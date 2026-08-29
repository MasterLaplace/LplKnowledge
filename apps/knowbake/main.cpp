/**
 * @file main.cpp
 * @brief Bake the local store into a .lplknow image.
 *
 * The writer half of the reader/writer line. Every index is computed here, once,
 * so the ring-0 reader never has to build one — it opens the image, answers a
 * bounded query, and allocates nothing. The same discipline that lets a 148-byte
 * cartridge rebuild a world.
 *
 * @warning The caller-side sketch that stood here named `lpl::graph::PossibleWorld` and
 * `lpl::harvest::Store`. It was written before `lpl::history` existed, and it is the sketch
 * that gave way: possible worlds are `history::WorldView` and are not a bake-time concern at
 * all, because an image carries every source and a WorldView decides AT READ TIME which of
 * them to listen to. `Store` is still ahead of the code — ingestion is the next lot — so
 * this tool bakes the one corpus that already exists, and says so rather than pretending to
 * read a store it has not got.
 *
 * `--header` is the other half of a checked-in artefact, and it exists because of a mistake
 * the project already made: `ParityPackBlob.hpp` and `ViewerPackBlob.hpp` had their
 * freshness VERIFIED with no path that could refresh them, so both had drifted into two
 * different hand-pasted layouts. A byte array in a tree needs a generator, or it rots.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include <lpl/Foundation.hpp>

#include <lpl/harvest/Baker.hpp>
#include <lpl/knowledge/KnowledgePack.hpp>

#include <lpl/harvest/Relief.hpp>
#include <lpl/harvest/ReliefStreamer.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

/**
 * @brief Prints how to use the tool.
 */
void usage()
{
    std::fprintf(stderr, "usage: lpl-knowbake --parity <out.lplknow>\n"
                         "       lpl-knowbake --header <symbol> - <out.hpp>\n"
                         "       lpl-knowbake --relief-header <tile.hgt> <reduce> <cells> <out.hpp>\n"
                         "       lpl-knowbake --relief-walk <base> <tileCells> <steps>\n"
                         "       lpl-knowbake --relief-bake <tile.hgt> <reduce> <tileCells> <base>\n"
                         "\n"
                         "  --parity   bake the canonical corpus of gate P13 into an image\n"
                         "  --header   emit that image as a C++ byte array; '-' means the parity\n"
                         "             corpus, which comes from code and is what makes it the\n"
                         "             reference rather than a document someone edited\n");
}

/**
 * @brief Writes bytes to a file.
 *
 * @param path  Where.
 * @param bytes What.
 * @return false when the file could not be written.
 */
[[nodiscard]] bool writeFile(const char *path, const std::vector<lpl::core::u8> &bytes)
{
    std::FILE *file = std::fopen(path, "wb");
    if (file == nullptr)
        return false;
    const bool ok = bytes.empty() || std::fwrite(bytes.data(), 1u, bytes.size(), file) == bytes.size();
    return std::fclose(file) == 0 && ok;
}

/**
 * @brief Rewrites only the ARRAY BODY of a generated header, keeping the prose around it.
 *
 * The surrounding comment is worth more than the bytes: it says why the blob exists, what
 * regenerates it, and what breaks when it is stale. A generator that rewrote the whole file
 * would delete that on every run, so this one finds the braces and replaces what is between
 * them. When the file does not exist yet it writes a complete one, prose included.
 *
 * @param path   The header.
 * @param symbol Array name.
 * @param bytes  What it should hold.
 * @return false when the file could not be written, or its shape was not recognised.
 */
[[nodiscard]] bool writeHeader(const char *path, const char *symbol, const std::vector<lpl::core::u8> &bytes)
{
    std::string body;
    body.reserve(bytes.size() * 6u);
    for (std::size_t i = 0u; i < bytes.size(); ++i)
    {
        char cell[8];
        std::snprintf(cell, sizeof(cell), "0x%02X,", static_cast<unsigned>(bytes[i]));
        if (i % 12u == 0u)
            body += "\n    ";
        body += cell;
        if (i % 12u != 11u && i + 1u != bytes.size())
            body += ' ';
    }
    body += "\n";

    std::string existing;
    if (std::FILE *file = std::fopen(path, "rb"); file != nullptr)
    {
        char chunk[4096];
        std::size_t read = 0u;
        while ((read = std::fread(chunk, 1u, sizeof(chunk), file)) > 0u)
            existing.append(chunk, read);
        (void) std::fclose(file);
    }

    std::string output;
    if (!existing.empty())
    {
        const std::string marker = std::string{symbol} + "[] = {";
        const std::size_t open = existing.find(marker);
        const std::size_t close = open == std::string::npos ? std::string::npos : existing.find("};", open);
        if (open == std::string::npos || close == std::string::npos)
        {
            std::fprintf(stderr, "lpl-knowbake: %s does not contain '%s'\n", path, marker.c_str());
            return false;
        }
        output = existing.substr(0u, open + marker.size()) + body + existing.substr(close);
    }
    else
    {
        const char *slash = std::strrchr(path, '/');
        output = "/**\n * @file ";
        output += slash != nullptr ? slash + 1 : path;
        output += "\n * @brief GENERATED — the canonical knowledge image, as bytes in the tree.\n"
                  " *\n"
                  " * Regenerate with:  lpl-knowbake --header ";
        output += symbol;
        output += " - <this file>\n"
                  " *\n"
                  " * A byte array rather than a file the kernel loads, for the same reason\n"
                  " * ParityPackBlob.hpp is one: a kernel build must require no host tool, and a gate\n"
                  " * that depended on a file being present would be a gate that skips itself when it is\n"
                  " * not. Do NOT hand-edit — the two pack blobs drifted into two different hand-pasted\n"
                  " * layouts precisely because nothing could regenerate them.\n"
                  " *\n"
                  " * @author MasterLaplace\n"
                  " * @copyright MIT License\n"
                  " */\n"
                  "\n#pragma once\n\n#include <lpl/Foundation.hpp>\n\nnamespace lpl::knowledge {\n\n"
                  "/// The canonical image of gate P18. Aligned, because a section is read as words.\n"
                  "alignas(16) inline constexpr core::u8 ";
        output += symbol;
        output += "[] = {";
        output += body;
        output += "};\n\n/// Bytes of @ref ";
        output += symbol;
        output += ".\ninline constexpr core::u32 ";
        output += symbol;
        output += "Size = static_cast<core::u32>(sizeof(";
        output += symbol;
        output += "));\n\n} // namespace lpl::knowledge\n";
    }

    std::FILE *file = std::fopen(path, "wb");
    if (file == nullptr)
        return false;
    const bool ok = std::fwrite(output.data(), 1u, output.size(), file) == output.size();
    return std::fclose(file) == 0 && ok;
}

/**
 * @brief Bakes the canonical corpus, reporting what happened.
 *
 * @param out Receives the image.
 * @return false when the bake was refused.
 */
[[nodiscard]] bool bakeCanonical(std::vector<lpl::core::u8> &out)
{
#if defined(LPL_HAS_FOUNDATION)
    lpl::harvest::BakeReport report{};
    if (!lpl::harvest::bakeParityCorpus(out, report))
    {
        std::fprintf(stderr, "lpl-knowbake: refused — %u collisions, %u unsourced claims%s%s\n", report.collisions,
                     report.unsourced, report.firstCollision.empty() ? "" : " — ", report.firstCollision.c_str());
        return false;
    }

    // Baked, then REOPENED before it is written. What a host writes, a constrained target has
    // to be able to open — one with no filesystem, no allocator to spare and no tolerance for
    // a bad input — so the tool proves that here instead of leaving the kernel to discover it
    // at boot.
    lpl::knowledge::KnowledgePack pack;
    const lpl::knowledge::OpenStatus status = pack.open(out.data(), static_cast<lpl::core::u32>(out.size()));
    if (status != lpl::knowledge::OpenStatus::Ok)
    {
        std::fprintf(stderr, "lpl-knowbake: baked an image it cannot reopen — %s\n",
                     lpl::knowledge::openStatusText(status));
        return false;
    }

    std::printf("baked %u bytes: %u facts, %u sources, %u documents, %u loci, %u names, %u sections\n", report.bytes,
                report.facts, report.sources, report.documents, report.loci, report.vocabulary, report.sections);
    return true;
#else
    (void) out;
    std::fprintf(stderr, "lpl-knowbake: --parity needs the LplPlugin foundation (the corpus lives in\n"
                         "              history::parityCorpus, and is deliberately not restated here)\n");
    return false;
#endif
}

} // namespace

/**
 * @brief Emits a window of real ground as a C++ array the kernel can hold.
 *
 * @warning **A raw array and not a `.lplknow` image, and the reason is the LINK ORDER.** The kernel
 * links `-lknowledge -lassistant -lengine -lkxx -lk`, and a static archive only satisfies references
 * already pending when the linker reaches it -- so `libengine`, which is where a World lives, cannot
 * call into `libknowledge`. A world that wanted to open an image would have to be relinked ahead of
 * the reader, which is a much larger decision than showing a landscape. The wire format is exercised
 * host-side instead, in `test-relief`.
 *
 * @warning **Regenerable, which is the whole point of it being a tool mode.** Two checked-in cartridges
 * in this project drifted into two different hand-edited layouts precisely because their freshness
 * was verified and nothing could refresh them.
 *
 * @param path    A `.hgt` tile.
 * @param reduce  Integer reduction factor; must divide the tile's intervals.
 * @param cells   Side of the window taken from the reduced tile.
 * @param symbol  C++ identifier prefix.
 * @param out     Header to write.
 * @return false on any failure, each named on stderr.
 */
bool writeReliefHeader(const char *path, unsigned reduce, unsigned cells, const char *out)
{
    lpl::harvest::ReliefTile tile;
    lpl::harvest::ReliefReadReport report{};
    if (!lpl::harvest::readHeightTile(path, tile, report))
    {
        std::fprintf(stderr, "lpl-knowbake: cannot read tile %s\n", path);
        return false;
    }
    lpl::harvest::ReliefTile reduced;
    if (!lpl::harvest::reduceTile(tile, reduce, reduced))
    {
        std::fprintf(stderr, "lpl-knowbake: reduce %u does not divide %u intervals\n", reduce,
                     tile.side - 1u);
        return false;
    }
    if (cells == 0u || cells > reduced.side)
    {
        std::fprintf(stderr, "lpl-knowbake: %u cells does not fit in a reduced side of %u\n", cells,
                     reduced.side);
        return false;
    }

    // The window is taken from the tile's NORTH-WEST corner, which is where its first sample is, so
    // the array's row order is the tile's row order and nothing is mirrored on the way out.
    long lowest = 0;
    long highest = 0;
    unsigned gaps = 0u;
    bool any = false;
    std::string body;
    for (unsigned r = 0u; r < cells; ++r)
    {
        body += "    ";
        for (unsigned c = 0u; c < cells; ++c)
        {
            const lpl::core::i16 metres = reduced.at(r, c);
            if (metres == lpl::harvest::kReliefVoid)
                ++gaps;
            else
            {
                if (!any || metres < lowest)
                    lowest = metres;
                if (!any || metres > highest)
                    highest = metres;
                any = true;
            }
            char cell[16];
            std::snprintf(cell, sizeof(cell), "%d,", static_cast<int>(metres));
            body += cell;
        }
        body += "\n";
    }

    std::string text =
        "/**\n"
        " * @file ReliefBlob.hpp\n"
        " * @brief A window of REAL ground, so a world can stand on measured earth in ring 0.\n"
        " *\n"
        " * @warning GENERATED. Do not hand-edit -- regenerate with the command below. Two checked-in\n"
        " * cartridges in this project drifted into two different hand-maintained layouts because their\n"
        " * freshness was checked and nothing could refresh them.\n"
        " *\n"
        " * @warning A raw array rather than a `.lplknow` image because the kernel links\n"
        " * `-lknowledge -lassistant -lengine -lkxx -lk`: a static archive only satisfies references\n"
        " * already pending when the linker reaches it, so `libengine` -- where a World lives -- cannot\n"
        " * call the image reader. The wire format is exercised host-side in `test-relief`.\n"
        " *\n"
        " * @author MasterLaplace\n"
        " * @version 0.1.0\n"
        " * @copyright MIT License\n"
        " */\n\n"
        "#pragma once\n\n"
        "#ifndef LPL_SAMPLES_RELIEFBLOB_HPP\n"
        "#    define LPL_SAMPLES_RELIEFBLOB_HPP\n\n"
        "#    include <lpl/core/Types.hpp>\n\n"
        "namespace lpl::samples {\n\n";

    // @warning The size is checked, not hoped for. snprintf TRUNCATES silently, and a 512-byte buffer
    // cut this block in the middle of an identifier -- producing a header that still looked like a
    // header, with `kRelie` followed by a row of samples. It failed to compile, which was luck: a
    // truncation one character later would have compiled and shipped a wrong constant.
    char meta[2048];
    const int metaLength = std::snprintf(meta, sizeof(meta),
                  "/// Samples per side of the window.\ninline constexpr core::u32 kReliefBlobSide = %uu;\n"
                  "/// Ground one sample covers, in metres: one arc-second times the reduction.\n"
                  "inline constexpr core::u32 kReliefBlobMetresPerCell = %uu;\n"
                  "/// South-west corner of the source tile, in whole degrees.\n"
                  "inline constexpr core::i32 kReliefBlobSouthLatitude = %d;\n"
                  "inline constexpr core::i32 kReliefBlobWestLongitude = %d;\n"
                  "/// Range actually present, in metres. Bathymetry makes the low end negative.\n"
                  "inline constexpr core::i32 kReliefBlobLowest = %ld;\n"
                  "inline constexpr core::i32 kReliefBlobHighest = %ld;\n"
                  "/// Samples nobody measured. Carried through, never filled.\n"
                  "inline constexpr core::u32 kReliefBlobGaps = %uu;\n\n"
                  "/// Elevation in metres, row-major, NORTH row first.\ninline constexpr core::i16 kReliefBlobSamples[] = {\n",
                  cells, 30u * reduce, tile.southLatitude, tile.westLongitude, lowest, highest, gaps);
    if (metaLength < 0 || static_cast<std::size_t>(metaLength) >= sizeof(meta))
    {
        std::fprintf(stderr, "lpl-knowbake: header metadata does not fit in %zu bytes\n", sizeof(meta));
        return false;
    }
    text += meta;
    text += body;
    text += "};\n\n} // namespace lpl::samples\n\n#endif // LPL_SAMPLES_RELIEFBLOB_HPP\n";

    std::FILE *file = std::fopen(out, "wb");
    if (file == nullptr)
    {
        std::fprintf(stderr, "lpl-knowbake: cannot write %s\n", out);
        return false;
    }
    std::fwrite(text.data(), 1u, text.size(), file);
    std::fclose(file);
    std::printf("wrote %s (%u x %u cells at %u m, %ld..%ld m, %u gaps, %zu bytes of source)\n", out,
                cells, cells, 30u * reduce, lowest, highest, gaps, text.size());
    return true;
}

/**
 * @brief Walks a baked survey and reports what streaming it costs.
 *
 * @warning **The consumer `math::planReliefResidency` and `harvest::ReliefStreamer` did not have.** A
 * residency policy nothing executes is a feature that cannot be wrong, and a streamer nothing drives
 * is a bound nobody has paid. This walks a real bake and says how many tiles were mapped, how many
 * released, and -- the number that matters -- how many steps found no ground under them.
 *
 * @param base      Path without extension, as `reliefTilePath` names the parts.
 * @param tileCells The size the survey was baked with. See `ReliefStreamer::configure`.
 * @param steps     How far to walk, in cells.
 * @return false when the survey answered for nothing at all, which means the bake or the path is
 *         wrong rather than that the walk went badly.
 */
bool walkRelief(const char *base, unsigned tileCells, unsigned steps)
{
    lpl::math::ReliefResidencyParams residency{};
    // @warning The size the survey was BAKED with, not a default. A plan in a lattice the bake never
    // used asks for files that are not there, so the walk finds no ground and calls it missing --
    // which reads exactly like walking off the edge of the survey rather than like a mismatch.
    residency.tileCells = tileCells;
    lpl::harvest::ReliefStreamer streamer;
    streamer.configure(base, residency);

    unsigned blind = 0u;
    unsigned answered = 0u;
    unsigned loaded = 0u;
    unsigned evicted = 0u;
    unsigned missing = 0u;
    unsigned rejected = 0u;
    unsigned peak = 0u;

    for (unsigned step = 0u; step < steps; ++step)
    {
        const lpl::core::i32 x = static_cast<lpl::core::i32>(step);
        const lpl::harvest::ReliefStreamReport report = streamer.update(x, 0);
        loaded += report.loaded;
        evicted += report.evicted;
        missing += report.missing;
        rejected += report.rejected;
        if (report.resident > peak)
            peak = report.resident;
        lpl::math::Fixed32 ground{};
        if (streamer.mosaic().heightAt(x, 0, ground))
            ++answered;
        else
            ++blind;
    }

    std::printf("walked %u cells: %u answered, %u with no ground, %u tiles loaded, %u released, "
                "%u missing, %u rejected, peak %u resident, %llu bytes mapped\n",
                steps, answered, blind, loaded, evicted, missing, rejected, peak,
                (unsigned long long) streamer.mappedBytes());
    if (rejected > 0u)
        std::fprintf(stderr, "lpl-knowbake: %u tile(s) were present but unreadable\n", rejected);
    return answered > 0u;
}

/**
 * @brief Bakes one elevation tile as a tiled survey a streamer can walk.
 *
 * @warning The other half of @ref walkRelief: a walk needs something to walk on, and a bake nobody
 * reads is the orphan this pair exists to close.
 *
 * @param path      A `.hgt` tile.
 * @param reduce    Integer reduction; must divide the tile's intervals.
 * @param tileCells Cells per output tile side.
 * @param base      Path without extension for the parts.
 * @return false on any failure, each named on stderr.
 */
bool bakeRelief(const char *path, unsigned reduce, unsigned tileCells, const char *base)
{
    lpl::harvest::ReliefTile tile;
    lpl::harvest::ReliefReadReport report{};
    if (!lpl::harvest::readHeightTile(path, tile, report))
    {
        std::fprintf(stderr, "lpl-knowbake: cannot read tile %s\n", path);
        return false;
    }
    lpl::harvest::ReliefTile reduced;
    if (!lpl::harvest::reduceTile(tile, reduce, reduced))
    {
        std::fprintf(stderr, "lpl-knowbake: reduce %u does not divide %u intervals\n", reduce,
                     tile.side - 1u);
        return false;
    }

    lpl::harvest::ReliefRegion region;
    if (!lpl::harvest::assembleRegion({reduced}, tile.southLatitude, tile.westLongitude, 1u, 1u,
                                      reduced.side - 1u, region))
    {
        std::fprintf(stderr, "lpl-knowbake: cannot assemble the region\n");
        return false;
    }

    lpl::math::GeoProjection spec{};
    // The tile names its SOUTH-west corner; a projection wants the NORTH-west one, and a tile is
    // one degree tall.
    spec.originLatitudeRaw = (tile.southLatitude + 1) * 65536;
    spec.originLongitudeRaw = tile.westLongitude * 65536;
    spec.referenceLatitude = tile.southLatitude;
    spec.metresPerCell = 30u * reduce;
    spec.unitsPerMetre = lpl::math::Fixed32::fromFloat(0.05f);
    spec.seaLevelUnits = lpl::math::Fixed32::fromFloat(-1.0f);
    const lpl::math::ReliefProjection projection = lpl::math::makeReliefProjection(spec);

    lpl::harvest::ReliefTilePlan plan{};
    plan.tileCells = tileCells;
    // Enough tiles to cover the degree at this cell size, rounded up: a plan short by one leaves a
    // strip of the survey unbaked, and a walk across it would find no ground with nothing to say why.
    const lpl::core::u32 across = (reduced.side + tileCells - 1u) / tileCells;
    plan.tilesX = across;
    plan.tilesZ = across;

    lpl::harvest::ReliefBakeReport bake{};
    if (!lpl::harvest::bakeReliefTiles(base, region, projection, plan, tileCells / 8u,
                                       lpl::harvest::reliefAttribution(), bake))
    {
        std::fprintf(stderr, "lpl-knowbake: the tiled bake failed\n");
        return false;
    }
    std::printf("baked %u tiles (%u empty), %llu bytes, %u cells, %u gaps\n", bake.tilesWritten,
                bake.tilesEmpty, (unsigned long long) bake.bytesWritten, bake.cells, bake.gaps);
    return bake.tilesWritten > 0u;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && std::strcmp(argv[1], "--parity") == 0)
    {
        std::vector<lpl::core::u8> image;
        if (!bakeCanonical(image))
            return 1;
        if (!writeFile(argv[2], image))
        {
            std::fprintf(stderr, "lpl-knowbake: cannot write %s\n", argv[2]);
            return 1;
        }
        std::printf("wrote %s\n", argv[2]);
        return 0;
    }

    if (argc >= 6 && std::strcmp(argv[1], "--relief-bake") == 0)
        return bakeRelief(argv[2], static_cast<unsigned>(std::atoi(argv[3])),
                          static_cast<unsigned>(std::atoi(argv[4])), argv[5])
                   ? 0
                   : 1;

    if (argc >= 5 && std::strcmp(argv[1], "--relief-walk") == 0)
        return walkRelief(argv[2], static_cast<unsigned>(std::atoi(argv[3])),
                          static_cast<unsigned>(std::atoi(argv[4])))
                   ? 0
                   : 1;

    if (argc >= 6 && std::strcmp(argv[1], "--relief-header") == 0)
    {
        return writeReliefHeader(argv[2], static_cast<unsigned>(std::atoi(argv[3])),
                                 static_cast<unsigned>(std::atoi(argv[4])), argv[5])
                   ? 0
                   : 1;
    }

    if (argc >= 5 && std::strcmp(argv[1], "--header") == 0)
    {
        std::vector<lpl::core::u8> image;
        if (std::strcmp(argv[3], "-") == 0)
        {
            if (!bakeCanonical(image))
                return 1;
        }
        else
        {
            std::FILE *file = std::fopen(argv[3], "rb");
            if (file == nullptr)
            {
                std::fprintf(stderr, "lpl-knowbake: cannot read %s\n", argv[3]);
                return 1;
            }
            lpl::core::u8 chunk[4096];
            std::size_t read = 0u;
            while ((read = std::fread(chunk, 1u, sizeof(chunk), file)) > 0u)
                image.insert(image.end(), chunk, chunk + read);
            (void) std::fclose(file);
        }

        if (!writeHeader(argv[4], argv[2], image))
            return 1;
        std::printf("wrote %s (%zu bytes as %s)\n", argv[4], image.size(), argv[2]);
        return 0;
    }

    usage();
    return 2;
}
