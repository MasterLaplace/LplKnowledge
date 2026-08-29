/**
 * @file Relief.cpp
 * @brief Reading real ground out of bare elevation tiles.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/Relief.hpp>

#include <lpl/harvest/Baker.hpp>

#include <cstdio>

namespace lpl::harvest {

namespace {

/**
 * Sides this reader recognises, and nothing else.
 *
 * @warning A closed list because the format cannot be validated from the inside: with no header, a
 * file of the wrong length read at a guessed side yields a perfectly plausible grid of the wrong
 * shape. 3601 is one arc-second, 1201 is three, 601 is a reduced product.
 */
constexpr core::u32 kKnownSides[] = {3601u, 1201u, 601u};

/**
 * @brief Whether a side is one this reader knows.
 *
 * @param side Samples per side.
 * @return true when it is recognised.
 */
[[nodiscard]] bool knownSide(core::u32 side) noexcept
{
    for (const core::u32 known : kKnownSides)
        if (known == side)
            return true;
    return false;
}

/**
 * @brief Reads a run of decimal digits.
 *
 * @param text   Where to read.
 * @param cursor Advanced past what was read.
 * @param count  Exactly how many digits to take.
 * @param out    Receives the value.
 * @return false when there were not that many digits.
 */
[[nodiscard]] bool digits(std::string_view text, std::size_t &cursor, std::size_t count, core::i32 &out) noexcept
{
    if (cursor + count > text.size())
        return false;
    core::i32 value = 0;
    for (std::size_t i = 0u; i < count; ++i)
    {
        const char c = text[cursor + i];
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + (c - '0');
    }
    cursor += count;
    out = value;
    return true;
}

} // namespace

std::string_view reliefAttribution() noexcept
{
    // Verbatim as the sources require. Concatenated rather than abbreviated: each line is a
    // condition of the permission to redistribute, and shortening one is dropping it.
    return "SRTM data courtesy of the U.S. Geological Survey. "
           "GMTED2010 data courtesy of the U.S. Geological Survey. "
           "DOC/NOAA/NESDIS/NCEI > National Centers for Environmental Information.";
}

bool tileCorner(std::string_view name, core::i32 &outLatitude, core::i32 &outLongitude) noexcept
{
    outLatitude = 0;
    outLongitude = 0;

    // The basename only: a path may contain letters that look like a corner.
    const std::size_t slash = name.find_last_of("/\\");
    if (slash != std::string_view::npos)
        name = name.substr(slash + 1u);
    if (name.size() < 7u)
        return false;

    std::size_t cursor = 0u;
    const char ns = name[cursor++];
    if (ns != 'N' && ns != 'S' && ns != 'n' && ns != 's')
        return false;
    core::i32 latitude = 0;
    if (!digits(name, cursor, 2u, latitude))
        return false;

    if (cursor >= name.size())
        return false;
    const char ew = name[cursor++];
    if (ew != 'E' && ew != 'W' && ew != 'e' && ew != 'w')
        return false;
    core::i32 longitude = 0;
    if (!digits(name, cursor, 3u, longitude))
        return false;

    // @warning The hemisphere is applied to the CORNER, and the corner is the south-west one. Getting
    // the sign wrong does not fail: it reads a real tile and puts it in the wrong hemisphere,
    // which looks like a world that simply has different mountains.
    outLatitude = (ns == 'S' || ns == 's') ? -latitude : latitude;
    outLongitude = (ew == 'W' || ew == 'w') ? -longitude : longitude;
    return true;
}

bool readHeightTile(const std::string &path, ReliefTile &out, ReliefReadReport &outReport)
{
    out = ReliefTile{};
    outReport = ReliefReadReport{};

    if (!tileCorner(path, out.southLatitude, out.westLongitude))
    {
        outReport.malformed = true;
        return false;
    }

    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return false;

    std::fseek(file, 0, SEEK_END);
    const long length = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (length <= 0 || (length % 2) != 0)
    {
        std::fclose(file);
        outReport.malformed = true;
        return false;
    }

    const core::u32 count = static_cast<core::u32>(length / 2);
    core::u32 side = 0u;
    for (const core::u32 known : kKnownSides)
    {
        if (known * known == count)
        {
            side = known;
            break;
        }
    }
    if (side == 0u || !knownSide(side))
    {
        std::fclose(file);
        outReport.malformed = true;
        return false;
    }

    std::vector<core::u8> raw(static_cast<core::usize>(length));
    const std::size_t read = std::fread(raw.data(), 1u, raw.size(), file);
    std::fclose(file);
    if (read != raw.size())
    {
        outReport.malformed = true;
        return false;
    }

    out.side = side;
    out.samples.resize(count);

    // @warning Big-endian to native, ONCE, here. The samples go on to cross into ring 0 through a
    // section, and a reader on the far side that had to know about byte order would be a reader
    // that can get it wrong on a target nobody tested.
    bool anyReal = false;
    for (core::u32 i = 0u; i < count; ++i)
    {
        const core::i16 value =
            static_cast<core::i16>((static_cast<core::u16>(raw[i * 2u]) << 8) | static_cast<core::u16>(raw[i * 2u + 1u]));
        out.samples[i] = value;
        if (value == kReliefVoid)
        {
            ++outReport.voids;
            continue;
        }
        if (!anyReal)
        {
            outReport.lowest = value;
            outReport.highest = value;
            anyReal = true;
            continue;
        }
        if (value < outReport.lowest)
            outReport.lowest = value;
        if (value > outReport.highest)
            outReport.highest = value;
    }
    return true;
}

bool reduceTile(const ReliefTile &tile, core::u32 factor, ReliefTile &out)
{
    out = ReliefTile{};
    if (factor == 0u || tile.side < 2u)
        return false;

    // A tile carries BOTH edges, so it has `side - 1` intervals. The factor must divide those, or
    // the last block runs off the end and the seam between two tiles stops lining up.
    const core::u32 intervals = tile.side - 1u;
    if ((intervals % factor) != 0u)
        return false;

    const core::u32 side = intervals / factor + 1u;
    out.southLatitude = tile.southLatitude;
    out.westLongitude = tile.westLongitude;
    out.side = side;
    out.samples.assign(static_cast<core::usize>(side) * side, kReliefVoid);

    for (core::u32 row = 0u; row < side; ++row)
    {
        for (core::u32 col = 0u; col < side; ++col)
        {
            // The block whose top-left corner is this output sample, clamped at the far edges so
            // the last row and column average what is actually there rather than reading past it.
            const core::u32 fromRow = row * factor;
            const core::u32 fromCol = col * factor;
            core::i64 sum = 0;
            core::u32 taken = 0u;
            for (core::u32 r = 0u; r < factor && fromRow + r < tile.side; ++r)
            {
                for (core::u32 c = 0u; c < factor && fromCol + c < tile.side; ++c)
                {
                    const core::i16 sample = tile.at(fromRow + r, fromCol + c);
                    if (sample == kReliefVoid)
                        continue;
                    sum += sample;
                    ++taken;
                }
            }
            // All void stays void: inventing a height for ground nobody measured is worse than
            // admitting the gap, because a fabricated height is indistinguishable from a real one.
            if (taken != 0u)
                out.samples[static_cast<core::usize>(row) * side + col] =
                    static_cast<core::i16>(sum / static_cast<core::i64>(taken));
        }
    }
    return true;
}

bool assembleRegion(const std::vector<ReliefTile> &tiles, core::i32 southLatitude, core::i32 westLongitude,
                    core::u32 widthDegrees, core::u32 heightDegrees, core::u32 samplesPerDegree,
                    ReliefRegion &out)
{
    out = ReliefRegion{};
    if (widthDegrees == 0u || heightDegrees == 0u || samplesPerDegree == 0u)
        return false;

    out.southLatitude = southLatitude;
    out.westLongitude = westLongitude;
    out.widthDegrees = widthDegrees;
    out.heightDegrees = heightDegrees;
    out.samplesPerDegree = samplesPerDegree;
    // @warning Both edges again: a region spanning two degrees at 120 samples a degree is 241 columns,
    // not 240. Writing 240 loses the eastern edge, and every subsequent sample is off by a
    // fraction that grows with distance.
    out.width = widthDegrees * samplesPerDegree + 1u;
    out.height = heightDegrees * samplesPerDegree + 1u;
    out.samples.assign(static_cast<core::usize>(out.width) * out.height, kReliefVoid);

    for (core::u32 degreeRow = 0u; degreeRow < heightDegrees; ++degreeRow)
    {
        for (core::u32 degreeCol = 0u; degreeCol < widthDegrees; ++degreeCol)
        {
            const core::i32 wantLatitude = southLatitude + static_cast<core::i32>(heightDegrees - 1u - degreeRow);
            const core::i32 wantLongitude = westLongitude + static_cast<core::i32>(degreeCol);

            const ReliefTile *found = nullptr;
            for (const ReliefTile &tile : tiles)
            {
                if (tile.southLatitude == wantLatitude && tile.westLongitude == wantLongitude)
                {
                    found = &tile;
                    break;
                }
            }
            // Refused rather than left void: a hole in a heightfield is a place a body falls
            // through, and a region that silently omits a degree looks like an ocean.
            if (found == nullptr || found->side < 2u)
                return false;

            const core::u32 intervals = found->side - 1u;
            if ((intervals % samplesPerDegree) != 0u)
                return false;
            const core::u32 step = intervals / samplesPerDegree;

            for (core::u32 r = 0u; r <= samplesPerDegree; ++r)
            {
                for (core::u32 c = 0u; c <= samplesPerDegree; ++c)
                {
                    const core::u32 outRow = degreeRow * samplesPerDegree + r;
                    const core::u32 outCol = degreeCol * samplesPerDegree + c;
                    if (outRow >= out.height || outCol >= out.width)
                        continue;
                    out.samples[static_cast<core::usize>(outRow) * out.width + outCol] =
                        found->at(r * step, c * step);
                }
            }
        }
    }
    return true;
}

bool resampleToCells(const ReliefRegion &region, const math::ReliefProjection &projection, CellGrid &out)
{
    out = CellGrid{};
    if (region.width == 0u || region.height == 0u || region.samplesPerDegree == 0u || region.samples.empty())
        return false;

    // The rectangle the region covers, in cells. Both corners are asked of the projection rather
    // than one corner plus a computed size: a size computed here would be a third opinion about how
    // big a degree is, beside the projection's and the region's.
    const core::i32 northLatitude = region.southLatitude + static_cast<core::i32>(region.heightDegrees);
    const core::i32 northRaw = northLatitude * 65536;
    const core::i32 westRaw = region.westLongitude * 65536;
    const core::i32 southRaw = region.southLatitude * 65536;
    const core::i32 eastRaw = (region.westLongitude + static_cast<core::i32>(region.widthDegrees)) * 65536;

    const core::i32 firstCol = projection.cellX(westRaw);
    const core::i32 lastCol = projection.cellX(eastRaw);
    const core::i32 firstRow = projection.cellZ(northRaw);
    const core::i32 lastRow = projection.cellZ(southRaw);
    if (lastCol < firstCol || lastRow < firstRow)
        return false;

    out.originCellX = firstCol;
    out.originCellZ = firstRow;
    out.width = static_cast<core::u32>(lastCol - firstCol + 1);
    out.height = static_cast<core::u32>(lastRow - firstRow + 1);
    out.samples.assign(static_cast<core::usize>(out.width) * out.height, kReliefVoid);

    bool anyReal = false;
    for (core::u32 row = 0u; row < out.height; ++row)
    {
        // The cell's own north edge, asked of the projection's inverse -- so a cell is sampled
        // where the reading side will think it is, and not half a cell away.
        const core::i32 latitudeRaw = projection.latitudeRawOf(firstRow + static_cast<core::i32>(row));
        // Degrees south of the region's north edge, at the region's own sample spacing. Integer
        // throughout: a float here would round differently on two machines baking one image.
        const core::i64 fromNorth = static_cast<core::i64>(northRaw) - static_cast<core::i64>(latitudeRaw);
        const core::i64 sampleRow = (fromNorth * region.samplesPerDegree) / 65536;

        for (core::u32 col = 0u; col < out.width; ++col)
        {
            const core::i32 longitudeRaw = projection.longitudeRawOf(firstCol + static_cast<core::i32>(col));
            const core::i64 fromWest = static_cast<core::i64>(longitudeRaw) - static_cast<core::i64>(westRaw);
            const core::i64 sampleCol = (fromWest * region.samplesPerDegree) / 65536;

            if (sampleRow < 0 || sampleCol < 0 || sampleRow >= static_cast<core::i64>(region.height) ||
                sampleCol >= static_cast<core::i64>(region.width))
            {
                ++out.gaps;
                continue;
            }

            // Nearest neighbour: it either lands on a measurement or lands on a gap, and says
            // which. An interpolation would mix a real height with the marker that means "nobody
            // measured", producing a smooth slope into ground nobody surveyed.
            const core::i16 metres =
                region.at(static_cast<core::u32>(sampleRow), static_cast<core::u32>(sampleCol));
            if (metres == kReliefVoid)
            {
                ++out.gaps;
                continue;
            }

            out.samples[static_cast<core::usize>(row) * out.width + col] = metres;
            if (!anyReal)
            {
                out.lowest = metres;
                out.highest = metres;
                anyReal = true;
                continue;
            }
            if (metres < out.lowest)
                out.lowest = metres;
            if (metres > out.highest)
                out.highest = metres;
        }
    }
    return true;
}

core::u32 ReliefTilePlan::exposedEdges(core::i32 tileX, core::i32 tileZ) const noexcept
{
    core::u32 mask = 0u;
    if (tileX <= firstTileX)
        mask |= math::kReliefEdgeWest;
    if (tileX >= firstTileX + static_cast<core::i32>(tilesX) - 1)
        mask |= math::kReliefEdgeEast;
    if (tileZ <= firstTileZ)
        mask |= math::kReliefEdgeNorth;
    if (tileZ >= firstTileZ + static_cast<core::i32>(tilesZ) - 1)
        mask |= math::kReliefEdgeSouth;
    return mask;
}

std::string reliefTilePath(const std::string &base, core::u32 level, core::i32 tileX, core::i32 tileZ)
{
    // Signs spelled as letters rather than as a minus, so a name never needs quoting and sorts the
    // way a human expects. Fixed width for the same reason a part index is: `x9` and `x10` sorting
    // out of order is how a set of tiles gets read in a surprising sequence.
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), ".L%u.%c%05d.%c%05d.lplknow", level, tileX < 0 ? 'w' : 'e',
                  tileX < 0 ? -tileX : tileX, tileZ < 0 ? 'n' : 's', tileZ < 0 ? -tileZ : tileZ);
    return base + buffer;
}

bool bakeReliefTiles(const std::string &base, const ReliefRegion &region,
                     const math::ReliefProjection &projection, const ReliefTilePlan &plan,
                     core::u32 blendCells, std::string_view credit, ReliefBakeReport &outReport)
{
    outReport = ReliefBakeReport{};
    if (plan.tileCells == 0u || plan.tilesX == 0u || plan.tilesZ == 0u)
        return false;

    // The whole survey in cell space, once. This is the one thing proportional to the region rather
    // than to a tile -- and it is the SOURCE, which a caller streams by assembling one rectangle of
    // .hgt files at a time. What this function guarantees is that nothing accumulates ACROSS tiles.
    CellGrid whole;
    if (!resampleToCells(region, projection, whole))
        return false;

    for (core::u32 tz = 0u; tz < plan.tilesZ; ++tz)
    {
        for (core::u32 tx = 0u; tx < plan.tilesX; ++tx)
        {
            const core::i32 tileX = plan.firstTileX + static_cast<core::i32>(tx);
            const core::i32 tileZ = plan.firstTileZ + static_cast<core::i32>(tz);

            CellGrid tile;
            tile.width = plan.tileCells;
            tile.height = plan.tileCells;
            tile.originCellX = tileX * static_cast<core::i32>(plan.tileCells);
            tile.originCellZ = tileZ * static_cast<core::i32>(plan.tileCells);
            tile.samples.assign(static_cast<core::usize>(plan.tileCells) * plan.tileCells, kReliefVoid);

            core::u32 real = 0u;
            for (core::u32 r = 0u; r < plan.tileCells; ++r)
            {
                for (core::u32 c = 0u; c < plan.tileCells; ++c)
                {
                    const core::i64 srcCol = static_cast<core::i64>(tile.originCellX) +
                                             static_cast<core::i64>(c) - whole.originCellX;
                    const core::i64 srcRow = static_cast<core::i64>(tile.originCellZ) +
                                             static_cast<core::i64>(r) - whole.originCellZ;
                    if (srcCol < 0 || srcRow < 0 || srcCol >= static_cast<core::i64>(whole.width) ||
                        srcRow >= static_cast<core::i64>(whole.height))
                        continue;
                    const core::i16 metres =
                        whole.samples[static_cast<core::usize>(srcRow) * whole.width +
                                      static_cast<core::usize>(srcCol)];
                    if (metres == kReliefVoid)
                        continue;
                    tile.samples[static_cast<core::usize>(r) * plan.tileCells + c] = metres;
                    ++real;
                }
            }

            // Skipped rather than written empty: "the survey has nothing here" and "this tile is all
            // sea" must stay different statements, and a reader given an empty image cannot tell.
            if (real == 0u)
            {
                ++outReport.tilesEmpty;
                continue;
            }

            Baker baker;
            baker.setRelief(tile, projection, blendCells);
            baker.setReliefExposedEdges(plan.exposedEdges(tileX, tileZ));
            if (!credit.empty())
                baker.addAttribution(credit);

            std::vector<core::u8> image;
            BakeReport bake{};
            if (!baker.build(image, bake))
                return false;

            const std::string path = reliefTilePath(base, 0u, tileX, tileZ);
            std::FILE *file = std::fopen(path.c_str(), "wb");
            if (file == nullptr)
                return false;
            const std::size_t written = std::fwrite(image.data(), 1u, image.size(), file);
            std::fclose(file);
            if (written != image.size())
                return false;

            ++outReport.tilesWritten;
            outReport.bytesWritten += image.size();
            outReport.cells += real;
            outReport.gaps += plan.tileCells * plan.tileCells - real;
            // `tile` and `image` die here, so the next tile starts from nothing. That is the bound.
        }
    }
    return true;
}

} // namespace lpl::harvest
