/**
 * @file test_relief.cpp
 * @brief Real ground read from bare tiles, and the four ways a headerless format lies.
 *
 * @warning Network-free: every fixture is written by this test. The real tiles were measured while the
 * reader was being written -- the southern Peloponnese comes back -4664 m in the Hellenic Trench
 * and 2390 m on Taygetus, whose true summit is 2404 -- but a test that needed to download a
 * quarter of a gigabyte would be a test nobody runs.
 *
 * @warning The format has NO HEADER. That is what makes it free to parse and also what makes it
 * impossible to validate from the inside: every failure below is one where a wrong reading
 * produces a perfectly plausible grid rather than an error.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/Baker.hpp>
#include <lpl/harvest/Relief.hpp>
#include <lpl/knowledge/KnowledgePack.hpp>
#include <lpl/harvest/ReliefStreamer.hpp>
#include <lpl/knowledge/ReliefSource.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int gChecks = 0;
int gFailures = 0;

/**
 * @brief Records one assertion.
 *
 * @param what Description.
 * @param ok   Whether it held.
 */
void check(const char *what, bool ok)
{
    ++gChecks;
    if (ok)
        return;
    ++gFailures;
    std::printf("  (fail) %s\n", what);
}

/**
 * @brief Writes a tile whose every sample says where it is.
 *
 * @warning Each sample encodes `row * 50 + col`, so a misread of the row order, the column order or
 * the byte order shows up as a WRONG PLACE rather than as noise. A fixture of constant height
 * would be read identically by every one of those mistakes.
 *
 * @warning The multiplier is 50 and not 100 because the product must stay inside an `i16`. A first
 * version used 100, so row 600 encoded 60 600, wrapped, and landed some samples exactly on
 * -32768 -- the value the format uses for "no measurement". A fixture that manufactures voids by
 * accident tests the void handling against noise it invented itself.
 *
 * @param path The file.
 * @param side Samples per side.
 */
void writeTile(const std::filesystem::path &path, lpl::core::u32 side)
{
    std::vector<unsigned char> bytes(static_cast<std::size_t>(side) * side * 2u);
    for (lpl::core::u32 row = 0u; row < side; ++row)
    {
        for (lpl::core::u32 col = 0u; col < side; ++col)
        {
            const lpl::core::i16 value = static_cast<lpl::core::i16>(row * 50 + col);
            const std::size_t at = (static_cast<std::size_t>(row) * side + col) * 2u;
            // BIG-endian, which is what the format is and what the reader must undo.
            bytes[at] = static_cast<unsigned char>((static_cast<lpl::core::u16>(value) >> 8) & 0xFFu);
            bytes[at + 1u] = static_cast<unsigned char>(static_cast<lpl::core::u16>(value) & 0xFFu);
        }
    }
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

int main()
{
    using namespace lpl;

    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lpl-relief-test";
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    std::printf("-- the corner is in the NAME, because the bytes do not say\n");
    {
        core::i32 latitude = 0;
        core::i32 longitude = 0;
        check("a northern eastern tile parses", harvest::tileCorner("N37E023.hgt", latitude, longitude));
        check("with its south-west corner", latitude == 37 && longitude == 23);

        // @warning The hemisphere applies to the corner, and getting it wrong does not fail: it reads
        // a real tile and puts it in the wrong hemisphere, which looks like a world that simply
        // has different mountains.
        check("a southern western tile parses", harvest::tileCorner("S34W071.hgt", latitude, longitude));
        check("and lands in the right hemisphere", latitude == -34 && longitude == -71);

        check("a path does not confuse it",
              harvest::tileCorner("/tmp/skadi/N36/N36E022.hgt", latitude, longitude) && latitude == 36 &&
                  longitude == 22);
        check("a name with no corner is refused", !harvest::tileCorner("elevation.hgt", latitude, longitude));
        check("and so is a truncated one", !harvest::tileCorner("N37E2.hgt", latitude, longitude));
    }

    std::printf("-- the shape is DEDUCED, and an unknown one is refused\n");
    {
        const std::filesystem::path good = root / "N10E010.hgt";
        writeTile(good, 601u);

        harvest::ReliefTile tile;
        harvest::ReliefReadReport report{};
        check("a known side reads", harvest::readHeightTile(good.string(), tile, report));
        check("with the side deduced from the byte count", tile.side == 601u);
        check("and the corner from the name", tile.southLatitude == 10 && tile.westLongitude == 10);

        // @warning The whole reason each sample encodes its own position: a misread of row order,
        // column order or byte order would still produce a grid, and only a positional fixture
        // says which one came back.
        check("the north-west sample is the first", tile.at(0u, 0u) == 0);
        check("rows run north to south", tile.at(1u, 0u) == 50);
        check("columns run west to east", tile.at(0u, 1u) == 1);
        check("and the bytes were big-endian", tile.at(3u, 4u) == 154);

        // A file whose length is not a known side squared must be REFUSED. Read at a guessed side
        // it yields a perfectly plausible grid of the wrong shape, which is the failure this
        // format cannot detect from the inside.
        const std::filesystem::path odd = root / "N11E011.hgt";
        {
            std::ofstream out(odd, std::ios::binary);
            const std::vector<char> junk(5000u, '\0');
            out.write(junk.data(), static_cast<std::streamsize>(junk.size()));
        }
        harvest::ReliefTile refused;
        harvest::ReliefReadReport refusedReport{};
        check("an unrecognised shape is refused", !harvest::readHeightTile(odd.string(), refused, refusedReport));
        check("and says so", refusedReport.malformed);

        check("a file that is not there is refused",
              !harvest::readHeightTile((root / "N12E012.hgt").string(), refused, refusedReport));
    }

    std::printf("-- a void is never averaged, and never invented\n");
    {
        const std::filesystem::path path = root / "N20E020.hgt";
        writeTile(path, 601u);

        harvest::ReliefTile tile;
        harvest::ReliefReadReport report{};
        check("the tile reads", harvest::readHeightTile(path.string(), tile, report));
        check("and has no voids", report.voids == 0u);

        // One void in a block, and one block entirely void.
        tile.samples[0] = harvest::kReliefVoid;
        for (core::u32 r = 0u; r < 4u; ++r)
            for (core::u32 c = 0u; c < 4u; ++c)
                tile.samples[static_cast<core::usize>(100u + r) * tile.side + (100u + c)] = harvest::kReliefVoid;

        harvest::ReliefTile reduced;
        check("it reduces", harvest::reduceTile(tile, 4u, reduced));

        // @warning A single void folded into a mean drags a whole coastal block to the bottom of the
        // sea -- a plausible number, in a place a reader would have to visit to doubt. The block
        // at the origin holds fifteen real samples and one void; its mean must be of the fifteen.
        core::i64 expected = 0;
        core::u32 taken = 0u;
        for (core::u32 r = 0u; r < 4u; ++r)
        {
            for (core::u32 c = 0u; c < 4u; ++c)
            {
                if (r == 0u && c == 0u)
                    continue;
                expected += r * 50 + c;
                ++taken;
            }
        }
        check("a void is left out of the mean", reduced.at(0u, 0u) == static_cast<core::i16>(expected / taken));

        // And a block with nothing real in it stays void: inventing a height for ground nobody
        // measured is worse than admitting the gap, because a fabricated height is
        // indistinguishable from a real one.
        check("an all-void block stays void", reduced.at(25u, 25u) == harvest::kReliefVoid);
    }

    std::printf("-- both edges belong to the tile, which is what makes seams line up\n");
    {
        const std::filesystem::path path = root / "N30E030.hgt";
        writeTile(path, 601u);

        harvest::ReliefTile tile;
        harvest::ReliefReadReport report{};
        check("the tile reads", harvest::readHeightTile(path.string(), tile, report));

        // @warning 601 samples is 600 INTERVALS, because the last column of one degree is the first of
        // the next. A factor that does not divide the intervals leaves the final block running off
        // the end, and the seam between two tiles stops lining up -- a cliff along every degree.
        harvest::ReliefTile reduced;
        check("a factor that divides the intervals is accepted", harvest::reduceTile(tile, 4u, reduced));
        check("and the reduced side keeps both edges", reduced.side == 600u / 4u + 1u);
        check("a factor that does not is refused", !harvest::reduceTile(tile, 7u, reduced));
        check("and so is zero", !harvest::reduceTile(tile, 0u, reduced));
    }

    std::printf("-- a region shares its seams, and refuses to have holes\n");
    {
        std::vector<harvest::ReliefTile> tiles;
        for (core::i32 lat = 40; lat <= 41; ++lat)
        {
            for (core::i32 lon = 50; lon <= 51; ++lon)
            {
                char name[32];
                std::snprintf(name, sizeof(name), "N%02dE%03d.hgt", lat, lon);
                const std::filesystem::path path = root / name;
                writeTile(path, 601u);
                harvest::ReliefTile tile;
                harvest::ReliefReadReport report{};
                if (harvest::readHeightTile(path.string(), tile, report))
                    tiles.push_back(std::move(tile));
            }
        }
        check("four tiles were read", tiles.size() == 4u);

        harvest::ReliefRegion region;
        check("they assemble", harvest::assembleRegion(tiles, 40, 50, 2u, 2u, 60u, region));
        // @warning Two degrees at sixty samples a degree is 121 columns, not 120: tiles SHARE their
        // edges. Concatenating instead would make the region one column wider per tile, and every
        // place east of the first seam would sit a fraction of a degree off.
        check("the region shares its seams", region.width == 121u && region.height == 121u);

        // A rectangle whose tiles are not all present is REFUSED. A hole in a heightfield is a
        // place a body falls through, and a region that silently omits a degree looks like ocean.
        harvest::ReliefRegion holed;
        check("a region missing a degree is refused", !harvest::assembleRegion(tiles, 40, 50, 3u, 2u, 60u, holed));
    }

    std::printf("-- the credit travels with the samples\n");
    {
        // @warning Redistribution is permitted BECAUSE of attribution. An image that carries these
        // samples without it is one nobody may pass on, so the text is a function of this module
        // rather than a line in a README that gets separated from the data.
        const std::string_view credit = harvest::reliefAttribution();
        check("there is an attribution", !credit.empty());
        check("it names the survey", credit.find("U.S. Geological Survey") != std::string_view::npos);
        check("and the ocean agency", credit.find("NOAA") != std::string_view::npos);
    }

    std::printf("-- the resampler puts a region on the world's cells, once\n");
    {
        // Two degrees of ground at a coarse spacing, every sample saying where it is.
        std::vector<harvest::ReliefTile> tiles;
        for (lpl::core::i32 lat = 36; lat <= 37; ++lat)
        {
            for (lpl::core::i32 lon = 22; lon <= 23; ++lon)
            {
                char name[32];
                std::snprintf(name, sizeof(name), "N%02dE%03d.hgt", lat, lon);
                writeTile(root / name, 601u);
                harvest::ReliefTile tile;
                harvest::ReliefReadReport report{};
                if (harvest::readHeightTile((root / name).string(), tile, report))
                    tiles.push_back(std::move(tile));
            }
        }
        harvest::ReliefRegion region;
        check("the region assembles", harvest::assembleRegion(tiles, 36, 22, 2u, 2u, 120u, region));

        lpl::math::GeoProjection spec{};
        // The NORTH-west corner: 36 + 2 degrees tall.
        spec.originLatitudeRaw = 38 * 65536;
        spec.originLongitudeRaw = 22 * 65536;
        spec.referenceLatitude = 37;
        spec.metresPerCell = 300u;
        spec.unitsPerMetre = lpl::math::Fixed32::fromFloat(0.05f);
        spec.seaLevelUnits = lpl::math::Fixed32::fromFloat(-1.0f);
        const lpl::math::ReliefProjection projection = lpl::math::makeReliefProjection(spec);

        harvest::CellGrid grid;
        check("it resamples onto cells", harvest::resampleToCells(region, projection, grid));
        check("and covers the ground it was given", grid.width > 0u && grid.height > 0u);

        // @warning A degree of longitude is SHORTER than one of latitude at this latitude, so the grid
        // must be narrower than it is tall. A resampler that ignored the projection would come out
        // square -- which still looks like a map, of a country the wrong shape.
        check("the grid is narrower than it is tall, as the latitude requires", grid.width < grid.height);

        std::printf("     %ux%u cells, %u gaps, %d..%d m\n", grid.width, grid.height, grid.gaps,
                    static_cast<int>(grid.lowest), static_cast<int>(grid.highest));
        check("nothing is missing behind it", grid.gaps == 0u);

        // @warning **The check that says WHICH sample landed where.** Shape and gap counts are
        // satisfied by any resampler that fills the grid, including one using arithmetic of its
        // own -- measured: a probe that replaced the projection with a hand-rolled column formula
        // passed every other assertion here. So a known geographic point is followed through
        // independently: the region's own geometry says which sample covers it, the forward
        // projection says which cell it falls in, and the grid must hold that sample there.
        {
            // A point a third of the way into the region, in degrees.
            const lpl::core::i32 latRaw = static_cast<lpl::core::i32>(36.7 * 65536.0);
            const lpl::core::i32 lonRaw = static_cast<lpl::core::i32>(22.4 * 65536.0);

            // Where the REGION holds it: rows run south from its north edge, columns east.
            const double fromNorth = 38.0 - 36.7;
            const double fromWest = 22.4 - 22.0;
            const auto srcRow = static_cast<lpl::core::u32>(fromNorth * 120.0);
            const auto srcCol = static_cast<lpl::core::u32>(fromWest * 120.0);
            const lpl::core::i16 expected = region.at(srcRow, srcCol);
            check("the region has a sample there", expected != harvest::kReliefVoid);

            // Where the PROJECTION puts it.
            const lpl::core::i32 cellX = projection.cellX(lonRaw);
            const lpl::core::i32 cellZ = projection.cellZ(latRaw);
            const auto col = static_cast<lpl::core::u32>(cellX - grid.originCellX);
            const auto row = static_cast<lpl::core::u32>(cellZ - grid.originCellZ);
            check("and the cell is inside the grid", col < grid.width && row < grid.height);

            // @warning The tolerance is one source COLUMN and no more, and that is what makes the check
            // discriminate. The fixture encodes `row * 50 + col`, so a one-column slip is worth 1
            // and a one-ROW slip is worth 50 -- a tolerance loose enough to allow a row is loose
            // enough to allow being half a kilometre north. Measured: the correct resampler is
            // exact here, and a probe that replaced the projection with its own column formula
            // came back -45. A first version allowed 60 and passed against that probe.
            const lpl::core::i16 got = grid.samples[static_cast<lpl::core::usize>(row) * grid.width + col];
            const int drift = static_cast<int>(got) - static_cast<int>(expected);
            check("a known point carries its own elevation", drift > -2 && drift < 2);
        }

        std::printf("-- and it survives a bake, a reopen, and being stood on\n");

        harvest::Baker baker;
        baker.setRelief(grid, projection, 4u);
        // @warning The credit is added by the CALLER, because this baker is handed numbers and cannot
        // tell a survey from a fixture. An image with samples and no credit is one nobody may pass
        // on, so the pairing is asserted rather than assumed.
        baker.addAttribution(harvest::reliefAttribution());
        harvest::BakeReport bake{};
        std::vector<lpl::core::u8> image;
        check("the image builds", baker.build(image, bake) && !image.empty());

        lpl::knowledge::KnowledgePack pack;
        check("and reopens", pack.open(image.data(), static_cast<lpl::core::u32>(image.size())) ==
                                 lpl::knowledge::OpenStatus::Ok);
        check("carrying its relief", pack.hasRelief());
        check("and its credit", pack.attributionCount() >= 1u);

        lpl::math::ReliefField field{};
        check("which becomes a field", lpl::knowledge::makeReliefField(pack, field));
        check("of the same shape", field.width == grid.width && field.height == grid.height);
        check("at the same origin",
              field.originCellX == grid.originCellX && field.originCellZ == grid.originCellZ);
        check("under the same blend", field.blendCells == 4u);

        // @warning The projection is READ from the image, not rebuilt. A reader that rebuilt it would be
        // a second projection, and the samples would sit in a different valley from the one they
        // were laid down in.
        check("and the same projection",
              field.projection.metresPerDegreeLongitudeQ16 == projection.metresPerDegreeLongitudeQ16 &&
                  field.projection.projection.metresPerCell == projection.projection.metresPerCell);

        // Every sample must survive the wire byte for byte: the section is where a big-endian slip
        // or a lost sign would turn a mountain into a trench.
        bool identical = true;
        for (lpl::core::usize i = 0u; i < grid.samples.size(); ++i)
            if (field.samples[i] != grid.samples[i])
                identical = false;
        check("every sample crossed the wire unchanged", identical);

        // And the whole point: the generator stands on it.
        lpl::math::Fixed32 height{};
        const lpl::core::i32 someX = grid.originCellX + static_cast<lpl::core::i32>(grid.width / 2u);
        const lpl::core::i32 someZ = grid.originCellZ + static_cast<lpl::core::i32>(grid.height / 2u);
        check("a cell in the middle answers", field.heightAt(someX, someZ, height));
        check("and sea level is still where the world put it",
              projection.worldHeightOf(0).raw() == spec.seaLevelUnits.raw());
    }

    std::printf("-- a survey bakes as tiles, and the tiles know they are neighbours\n");
    {
        std::vector<harvest::ReliefTile> tiles;
        for (lpl::core::i32 lat = 36; lat <= 37; ++lat)
            for (lpl::core::i32 lon = 22; lon <= 23; ++lon)
            {
                char name[32];
                std::snprintf(name, sizeof(name), "N%02dE%03d.hgt", lat, lon);
                writeTile(root / name, 601u);
                harvest::ReliefTile t;
                harvest::ReliefReadReport rep{};
                if (harvest::readHeightTile((root / name).string(), t, rep))
                    tiles.push_back(std::move(t));
            }
        harvest::ReliefRegion region;
        check("the region assembles", harvest::assembleRegion(tiles, 36, 22, 2u, 2u, 120u, region));

        lpl::math::GeoProjection spec{};
        spec.originLatitudeRaw = 38 * 65536;
        spec.originLongitudeRaw = 22 * 65536;
        spec.referenceLatitude = 37;
        spec.metresPerCell = 600u;
        spec.unitsPerMetre = lpl::math::Fixed32::fromFloat(0.05f);
        spec.seaLevelUnits = lpl::math::Fixed32::fromFloat(-1.0f);
        const lpl::math::ReliefProjection projection = lpl::math::makeReliefProjection(spec);

        harvest::ReliefTilePlan plan{};
        plan.tileCells = 64u;
        plan.tilesX = 2u;
        plan.tilesZ = 3u;

        // @warning Only the OUTSIDE of the plan is exposed. An inner tile that fades on every side
        // rings itself in half-invented ground, so the world comes out cross-hatched with a gentle
        // valley at every boundary -- terrain-looking, and wrong.
        check("the north-west tile faces nothing west or north",
              plan.exposedEdges(0, 0) == (lpl::math::kReliefEdgeWest | lpl::math::kReliefEdgeNorth));
        check("the middle-left tile faces a neighbour to the south",
              (plan.exposedEdges(0, 1) & lpl::math::kReliefEdgeSouth) == 0u);
        check("and one to the north", (plan.exposedEdges(0, 1) & lpl::math::kReliefEdgeNorth) == 0u);
        check("the south-east tile faces nothing east or south",
              plan.exposedEdges(1, 2) == (lpl::math::kReliefEdgeEast | lpl::math::kReliefEdgeSouth));

        const std::string base = (root / "earth").string();
        harvest::ReliefBakeReport bake{};
        check("the survey bakes as tiles",
              harvest::bakeReliefTiles(base, region, projection, plan, 8u, harvest::reliefAttribution(), bake));
        std::printf("     %u tiles written, %u empty, %llu bytes, %u cells\n", bake.tilesWritten,
                    bake.tilesEmpty, (unsigned long long) bake.bytesWritten, bake.cells);
        check("more than one tile was written", bake.tilesWritten > 1u);

        // @warning Both ends must agree on the naming by DECLARATION, not by coincidence: a writer and
        // a reader that each spell the convention are a bake nobody can find.
        lpl::core::u32 found = 0u;
        for (lpl::core::u32 tz = 0u; tz < plan.tilesZ; ++tz)
            for (lpl::core::u32 tx = 0u; tx < plan.tilesX; ++tx)
                if (std::filesystem::exists(harvest::reliefTilePath(base, 0u,
                                                                    static_cast<lpl::core::i32>(tx),
                                                                    static_cast<lpl::core::i32>(tz))))
                    ++found;
        check("every written tile is where the convention says", found == bake.tilesWritten);

        // And each tile reopens as a field that remembers which of its edges face nothing.
        const std::string first = harvest::reliefTilePath(base, 0u, 0, 0);
        std::ifstream in(first, std::ios::binary);
        std::vector<lpl::core::u8> bytes((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());
        lpl::knowledge::KnowledgePack pack;
        check("a tile reopens",
              pack.open(bytes.data(), static_cast<lpl::core::u32>(bytes.size())) ==
                  lpl::knowledge::OpenStatus::Ok);
        lpl::math::ReliefField field{};
        check("and becomes a field", lpl::knowledge::makeReliefField(pack, field));
        check("carrying its exposed edges across the wire",
              field.exposedEdges == plan.exposedEdges(0, 0));
        check("and its credit", pack.attributionCount() >= 1u);

        // The claim the tiling exists for: two neighbouring tiles meet with no fade between them.
        const std::string second = harvest::reliefTilePath(base, 0u, 1, 0);
        std::ifstream in2(second, std::ios::binary);
        std::vector<lpl::core::u8> bytes2((std::istreambuf_iterator<char>(in2)),
                                          std::istreambuf_iterator<char>());
        lpl::knowledge::KnowledgePack pack2;
        lpl::math::ReliefField field2{};
        if (pack2.open(bytes2.data(), static_cast<lpl::core::u32>(bytes2.size())) ==
                lpl::knowledge::OpenStatus::Ok &&
            lpl::knowledge::makeReliefField(pack2, field2))
        {
            lpl::math::ReliefMosaic mosaic{};
            check("both tiles are resident", mosaic.add(&field) && mosaic.add(&field2));
            const lpl::core::i32 seam = static_cast<lpl::core::i32>(plan.tileCells);
            bool solid = true;
            for (lpl::core::i32 z = 16; z < 48; ++z)
                for (lpl::core::i32 x = seam - 4; x <= seam + 3; ++x)
                    if (mosaic.weightAt(x, z).raw() != lpl::math::Fixed32::one().raw())
                        solid = false;
            check("and the seam between them does not fade", solid);
        }
    }

    std::printf("-- the streamer executes a plan, and the ground never vanishes underfoot\n");
    {
        // A survey wide enough to walk across: 6x4 tiles of 32 cells.
        const std::string base = (root / "streamed").string();
        constexpr lpl::core::u32 kTile = 32u;
        harvest::ReliefTilePlan plan{};
        plan.tileCells = kTile;
        plan.tilesX = 6u;
        plan.tilesZ = 4u;

        lpl::math::GeoProjection spec{};
        spec.originLatitudeRaw = 38 * 65536;
        spec.originLongitudeRaw = 22 * 65536;
        spec.referenceLatitude = 37;
        spec.metresPerCell = 600u;
        spec.unitsPerMetre = lpl::math::Fixed32::fromFloat(0.05f);
        spec.seaLevelUnits = lpl::math::Fixed32::fromFloat(-1.0f);
        const lpl::math::ReliefProjection proj = lpl::math::makeReliefProjection(spec);

        // Every tile written by hand, so the fixture does not depend on a region resample.
        for (lpl::core::u32 tz = 0u; tz < plan.tilesZ; ++tz)
        {
            for (lpl::core::u32 tx = 0u; tx < plan.tilesX; ++tx)
            {
                harvest::CellGrid g;
                g.width = kTile;
                g.height = kTile;
                g.originCellX = static_cast<lpl::core::i32>(tx * kTile);
                g.originCellZ = static_cast<lpl::core::i32>(tz * kTile);
                g.samples.assign(kTile * kTile, 0);
                for (lpl::core::u32 r = 0u; r < kTile; ++r)
                    for (lpl::core::u32 c = 0u; c < kTile; ++c)
                        g.samples[r * kTile + c] =
                            static_cast<lpl::core::i16>((g.originCellZ + static_cast<int>(r)) * 10 +
                                                        g.originCellX + static_cast<int>(c));
                harvest::Baker b;
                b.setRelief(g, proj, 4u);
                b.setReliefExposedEdges(plan.exposedEdges(static_cast<lpl::core::i32>(tx),
                                                          static_cast<lpl::core::i32>(tz)));
                std::vector<lpl::core::u8> img;
                harvest::BakeReport br{};
                if (!b.build(img, br))
                    continue;
                std::ofstream out(harvest::reliefTilePath(base, 0u, static_cast<lpl::core::i32>(tx),
                                                          static_cast<lpl::core::i32>(tz)),
                                  std::ios::binary);
                out.write(reinterpret_cast<const char *>(img.data()),
                          static_cast<std::streamsize>(img.size()));
            }
        }

        lpl::math::ReliefResidencyParams res{};
        res.tileCells = kTile;
        res.fineRadiusTiles = 1u;
        res.levels = 1u;   // only level 0 exists on disk here
        res.radiusPerLevel = 1u;

        harvest::ReliefStreamer streamer;
        streamer.configure(base, res);

        const harvest::ReliefStreamReport first = streamer.update(48, 48);
        check("the streamer loads what the plan asked for", first.loaded > 0u);
        check("and holds it", streamer.residentCount() == first.resident);
        check("nothing was rejected", first.rejected == 0u);
        std::printf("     resident=%u loaded=%u missing=%u bytes=%llu\n", first.resident, first.loaded,
                    first.missing, (unsigned long long) streamer.mappedBytes());

        // @warning Standing still must cost NOTHING. A streamer that rebuilt every frame would remap
        // the same unchanged tiles, and the cost of standing still would be the cost of travelling.
        const harvest::ReliefStreamReport again = streamer.update(48, 48);
        check("standing still loads nothing", again.loaded == 0u);
        check("and evicts nothing", again.evicted == 0u);

        // The ground the streamer offers must be the ground that was baked.
        lpl::math::Fixed32 h{};
        check("the mosaic answers under the eye", streamer.mosaic().heightAt(48, 48, h));
        check("with the ground that was baked", h.raw() == proj.worldHeightOf(48 * 10 + 48).raw());

        // @warning **THE control: walk, and the ground must never vanish underfoot.** Every failure
        // this whole design guards against shows up here -- a mapping released while its field is
        // still in the mosaic, a plan that drops the near tile instead of the far one, an eviction
        // that outruns its load. None of them raise anything; the ground simply stops answering.
        lpl::core::u32 steps = 0u;
        lpl::core::u32 blind = 0u;
        lpl::core::u32 wrong = 0u;
        lpl::core::u32 totalLoaded = 0u;
        lpl::core::u32 totalEvicted = 0u;
        for (lpl::core::i32 x = 40; x < 150; x += 2)
        {
            const harvest::ReliefStreamReport step = streamer.update(x, 48);
            totalLoaded += step.loaded;
            totalEvicted += step.evicted;
            ++steps;
            lpl::math::Fixed32 under{};
            if (!streamer.mosaic().heightAt(x, 48, under))
                ++blind;
            else if (under.raw() != proj.worldHeightOf(48 * 10 + x).raw())
                ++wrong;
        }
        std::printf("     walked %u steps: %u loaded, %u evicted, %u blind, %u wrong\n", steps,
                    totalLoaded, totalEvicted, blind, wrong);
        check("the ground never vanished underfoot", blind == 0u);
        check("and it was never the wrong ground", wrong == 0u);
        check("tiles really did stream in", totalLoaded > 0u);
        check("and out", totalEvicted > 0u);

        // Residency is BOUNDED: walking a long way must not accumulate mappings.
        check("residency stays bounded while walking", streamer.residentCount() <= 9u);

        // Off the edge of the survey, missing tiles are counted and not confused with corrupt ones.
        const harvest::ReliefStreamReport outside = streamer.update(10000, 10000);
        check("beyond the survey the tiles are simply missing", outside.missing > 0u);
        check("and none of them are rejected", outside.rejected == 0u);
        check("so the mosaic is empty rather than wrong",
              !streamer.mosaic().heightAt(10000, 10000, h));

        // @warning **The mosaic must hold exactly the resident slots, and this is asserted on COUNTS
        // rather than by reading through it.** A mosaic left stale after an eviction points into
        // released mappings, and reading that is undefined behaviour which frequently looks fine:
        // measured, a probe that never rebuilt the mosaic passed every walk assertion above,
        // because the freed slot memory was handed straight back to the next allocation and the
        // dangling pointers landed on valid fields. Counts cannot be rescued by luck.
        check("the mosaic holds exactly the resident tiles",
              streamer.mosaic().count == streamer.residentCount());
        check("which beyond the survey is none at all", streamer.residentCount() == 0u);
    }

    std::filesystem::remove_all(root, error);

    std::printf("\n%s (%d failures, %d checks)\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures,
                gChecks);
    return gFailures == 0 ? 0 : 1;
}
