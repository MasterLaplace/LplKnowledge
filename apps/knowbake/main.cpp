/**
 * @file main.cpp
 * @brief Bake the local store into a .lplknow image.
 *
 * The writer half of the reader/writer line. Every index is computed here, once,
 * so the ring-0 reader never has to build one — it opens the image, answers a
 * bounded query, and allocates nothing. The same discipline that lets a 148-byte
 * cartridge rebuild a world.
 *
 * ⚠ The caller-side sketch that stood here named `lpl::graph::PossibleWorld` and
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

#include <cstdio>
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
