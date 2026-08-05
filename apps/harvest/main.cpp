/**
 * @file main.cpp
 * @brief Harvest a corpus into a .lplknow image.
 *
 * ⚠ The caller-side sketch that stood here drove `Snapshot`, `Changefile` and
 * `OpenAccess` — the bulk-transfer path of chantier A. Every one of those speaks HTTP or
 * S3, and this tool ships without them for a reason worth stating rather than hiding: a
 * harvester that cannot be run cannot be checked, and shipping one would be shipping a
 * failure behind a confident interface. They arrive with the network lot.
 *
 * What it DOES harvest is the corpus that is already here and needs nothing: the project's
 * own markdown. That is not a consolation prize — it is the only corpus in reach that
 * exists, carries no licence constraint, is small enough that a whole image fits in a byte
 * array a kernel can carry, and has a reader on day one.
 *
 * And it answers a question a hand-maintained index cannot: **does every cited identifier
 * still point at a line that exists?** `EXTRACTION.md` holds hundreds of them, each with a
 * file and a line, all maintained by hand — which is precisely why such an index drifts.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include <lpl/Foundation.hpp>

#include <lpl/harvest/Baker.hpp>
#include <lpl/harvest/Markdown.hpp>
#include <lpl/knowledge/KnowledgePack.hpp>
#include <lpl/knowledge/Provenance.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

/**
 * @brief Prints how to use the tool.
 */
void usage()
{
    std::fprintf(stderr,
                 "usage: lpl-ingest [--strict] <out.lplknow> <root> [<root> ...]\n"
                 "\n"
                 "Reads every *.md under each root and bakes a .lplknow image: the stable\n"
                 "identifiers each document defines, who cites them, and the dates on the\n"
                 "headings that carry them. Structural only — pulling claims out of prose\n"
                 "needs a model, and guessing at it would produce confidences that mean\n"
                 "nothing.\n"
                 "\n"
                 "  --strict  refuse to write when a citation resolves to nothing, or when\n"
                 "            an identifier is defined twice. Off by default because a\n"
                 "            corpus mid-edit legitimately has both, and a tool that only\n"
                 "            ever refuses is a tool nobody runs.\n");
}

/**
 * @brief Is this directory outside the corpus?
 *
 * Three kinds of thing live in a repository and are not part of what it KNOWS, and lumping
 * them in was measured rather than feared — a first run read 711 documents, of which about
 * five hundred were somebody else's:
 *
 *   - **build output and version control** (`build`, `dist`, `.git`, `.xmake`) — nothing in
 *     them was authored;
 *   - **generated views** (`site/src/content/book`, whose files `split-book.mjs` writes from
 *     the book) — a generated file is not a source, and citing one is provenance pointing
 *     at something nobody wrote;
 *   - **vendored third-party repositories** (`repos_storage`, `node_modules`) — llama.cpp's
 *     documentation is real documentation and it is not this project's knowledge. Ingesting
 *     it does not enrich the corpus, it drowns it: an identifier scheme from another
 *     project arrives as hundreds of citations of things this corpus never defined.
 *
 * @param name A directory name.
 * @return true when its contents are not corpus.
 */
[[nodiscard]] bool skipped(const std::string &name)
{
    static const char *const kOutside[] = {
        "build", "dist", ".git", ".xmake", "node_modules", // built or vendored by a tool
        "repos_storage",                                   // cloned third-party repositories
        "legacy_backup",                                   // code kept out of the build tree
        ".Test",                                           // superseded copies of docs/research_reports
        "book",                                            // site/src/content/book: generated from the book
    };
    for (const char *candidate : kOutside)
        if (name == candidate)
            return true;
    return false;
}

/**
 * @brief Collects every markdown file under a root.
 *
 * Skips what is not corpus; see @ref skipped for the three kinds and why each is out.
 *
 * @param root   Where to look.
 * @param prefix What to prepend to each canonical name.
 * @param out    Receives the sources.
 * @return false when the root does not exist.
 */
[[nodiscard]] bool collect(const std::filesystem::path &root, const std::string &prefix,
                           std::vector<lpl::harvest::MarkdownSource> &out)
{
    std::error_code error;
    if (!std::filesystem::is_directory(root, error))
        return false;

    std::vector<std::filesystem::path> found;
    for (std::filesystem::recursive_directory_iterator
             it{root, std::filesystem::directory_options::skip_permission_denied, error},
         end;
         it != end; it.increment(error))
    {
        if (error)
            break;
        const std::string name = it->path().filename().string();
        if (it->is_directory(error) && skipped(name))
        {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(error) || it->path().extension() != ".md")
            continue;
        found.push_back(it->path());
    }

    // Sorted, because a directory walk has no defined order and the image must be a function
    // of the corpus rather than of the filesystem: two machines harvesting the same tree have
    // to produce the same bytes.
    std::sort(found.begin(), found.end());

    for (const std::filesystem::path &path : found)
    {
        const std::string relative = std::filesystem::relative(path, root, error).generic_string();
        out.push_back(lpl::harvest::MarkdownSource{path.string(), prefix + "/" + relative});
    }
    return true;
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

} // namespace

int main(int argc, char **argv)
{
    // --store is GONE, and its absence is the design rather than a removal. Its job was to
    // gather scattered documents into one place; the author gathered them by MOVING them, so a
    // copy would only be a second version of a file one directory away. What still lives
    // elsewhere — a CLAUDE.md loaded from a project root, a README rendered where it sits, the
    // book split from its own path — is read in place, which is what this tool does anyway.
    bool strict = false;
    int first = 1;
    while (first < argc && argv[first][0] == '-' && argv[first][1] == '-')
    {
        if (std::strcmp(argv[first], "--strict") == 0)
        {
            strict = true;
            ++first;
        }
        else
        {
            usage();
            return 2;
        }
    }
    if (argc < first + 2)
    {
        usage();
        return 2;
    }

    const char *output = argv[first];

    std::vector<lpl::harvest::MarkdownSource> sources;
    for (int i = first + 1; i < argc; ++i)
    {
        const std::filesystem::path root{argv[i]};
        // The canonical name carries the root's own directory name, so a document cited as
        // "LplKernel/CLAUDE.md" stays distinguishable from "LplPlugin/CLAUDE.md". Without it,
        // two repositories with the same layout would collide on every path.
        if (!collect(root, root.filename().string(), sources))
        {
            std::fprintf(stderr, "lpl-ingest: %s is not a directory\n", argv[i]);
            return 1;
        }
    }

    if (sources.empty())
    {
        std::fprintf(stderr, "lpl-ingest: no markdown found under the given roots\n");
        return 1;
    }

    lpl::harvest::Baker baker;
    lpl::harvest::IngestReport report{};
    if (!lpl::harvest::ingestMarkdown(sources, baker, report))
    {
        std::fprintf(stderr, "lpl-ingest: a document could not be read, or two names collided\n");
        return 1;
    }

    std::printf("read %u documents, %u lines, %u headings (%u dated)\n", report.documents, report.lines,
                report.headings, report.dated);
    std::printf("  %u identifiers defined, %u citations\n", report.definitions, report.citations);
    if (report.dangling != 0u)
        std::printf("  /!\\ %u citations resolve to nothing - first: %s\n", report.dangling,
                    report.firstDangling.c_str());
    if (report.duplicates != 0u)
        std::printf("  /!\\ %u identifiers defined twice - first: %s\n", report.duplicates,
                    report.firstDuplicate.c_str());

    if (strict && (report.dangling != 0u || report.duplicates != 0u))
    {
        std::fprintf(stderr, "lpl-ingest: --strict, and the corpus is not consistent\n");
        return 1;
    }

    std::vector<lpl::core::u8> image;
    lpl::harvest::BakeReport bake{};
    if (!baker.build(image, bake))
    {
        std::fprintf(stderr, "lpl-ingest: refused - %u collisions, %u unsourced claims%s%s\n", bake.collisions,
                     bake.unsourced, bake.firstCollision.empty() ? "" : " - ", bake.firstCollision.c_str());
        return 1;
    }

    // Baked, then REOPENED before it is written: what a host writes, a constrained target has
    // to be able to open, and proving that here beats leaving a kernel to discover it at boot.
    lpl::knowledge::KnowledgePack pack;
    const lpl::knowledge::OpenStatus status = pack.open(image.data(), static_cast<lpl::core::u32>(image.size()));
    if (status != lpl::knowledge::OpenStatus::Ok)
    {
        std::fprintf(stderr, "lpl-ingest: baked an image it cannot reopen - %s\n",
                     lpl::knowledge::openStatusText(status));
        return 1;
    }

    lpl::knowledge::ProvenanceAudit audit{};
    if (!lpl::knowledge::auditProvenance(pack, audit))
    {
        std::fprintf(stderr, "lpl-ingest: %u unsourced, %u dangling loci, %u dangling documents\n",
                     audit.unsourced, audit.danglingLocus, audit.danglingDocument);
        return 1;
    }

    if (!writeFile(output, image))
    {
        std::fprintf(stderr, "lpl-ingest: cannot write %s\n", output);
        return 1;
    }

    std::printf("wrote %s - %u bytes: %u facts, %u sources, %u documents, %u loci, %u names, %u text lines\n",
                output, bake.bytes, bake.facts, bake.sources, bake.documents, bake.loci, bake.vocabulary,
                bake.textLines);
    return 0;
}
