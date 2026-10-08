/**
 * @file main.cpp
 * @brief Harvest a corpus into a .lplknow image.
 *
 * @warning The caller-side sketch that stood here drove `Snapshot`, `Changefile` and
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

#include "Identity.hpp"

#include <lpl/Foundation.hpp>

#include <lpl/harvest/Baker.hpp>
#include <lpl/harvest/Markdown.hpp>
#include <lpl/harvest/ResearchReport.hpp>
#include <lpl/harvest/Catalogue.hpp>
#include <lpl/harvest/CatalogueStream.hpp>
#include <lpl/harvest/OaiPmh.hpp>
#include <lpl/harvest/Tei.hpp>
#include <lpl/knowledge/KnowledgePack.hpp>
#include <lpl/corpus/Urn.hpp>
#include <lpl/knowledge/Provenance.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
                 "  --include-store  also read LplKnowledge's own store/, which holds this\n"
                 "            project's working documents and the private conversations they\n"
                 "            came from. Off by default: the safe reading is the one that\n"
                 "            happens when nobody thought about it.\n"
                 "  --strict  refuse to write when a citation resolves to nothing, or when\n"
                 "            an identifier is defined twice. Off by default because a\n"
                 "            corpus mid-edit legitimately has both, and a tool that only\n"
                 "            ever refuses is a tool nobody runs.\n"
                 "\n"
                 "Holdings — a catalogue of what exists and where, rather than the texts:\n"
                 "  lpl-ingest --stream --hathi <file.txt> <out.lplknow>\n"
                 "  lpl-ingest --stream --oai <endpoint> [--oai-set S] [--oai-from DATE]\n"
                 "             [--oai-pages N] [--max-part BYTES] <out.lplknow>\n"
                 "\n"
                 "  --oai-from  harvest only what changed since DATE (YYYY-MM-DD). The run\n"
                 "            prints the date to pass next time; that is the whole of\n"
                 "            incremental synchronisation, because OAI-PMH already has it.\n"
                 "  --oai-pages  stop after N responses, for sampling a repository rather\n"
                 "            than mirroring it. A resumption token is printed to carry on.\n"
                 "  --version  print the version, commit and build of this tool, and the\n"
                 "            LplPlugin it was built with.\n");
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
[[nodiscard]] bool skipped(const std::string &name, bool includeStore)
{
    static const char *const kOutside[] = {
        "build", "dist", ".git", ".xmake", "node_modules", // built or vendored by a tool
        "repos_storage",                                   // cloned third-party repositories
        "legacy_backup",                                   // code kept out of the build tree
        ".Test",                                           // superseded copies of docs/research_reports
        "book",                                            // site/src/content/book: generated from the book
        // @warning The harvester's own store. It holds this project's working documents — plans,
        // audits, and the private conversations they came out of — which is why the
        // repository gitignores it. Reading it was measured, not feared: 95 documents, so
        // every count this tool printed was most of the way private material.
        //
        // @warning And the note in CLAUDE.md claiming a previous session had already excluded it
        // was FALSE — it was in neither the commit nor any working copy. A documented fix
        // that does not exist is worse than the bug, because it is what stops anyone looking.
    };
    // The harvester's own store is opt-IN rather than opt-out, and the asymmetry is the
    // point: the default has to be the one that is safe to run without thinking, and reading
    // a directory full of private conversations is not that. `--include-store` is how the
    // author indexes their own memory when they mean to.
    if (!includeStore && name == "store")
        return true;
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
                           std::vector<lpl::harvest::MarkdownSource> &out, bool includeStore)
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
        if (it->is_directory(error) && skipped(name, includeStore))
        {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(error))
            continue;
        const std::string extension = it->path().extension().string();
        if (extension != ".md" && extension != ".xml")
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

/**
 * @brief Reads the head of a file, enough to tell what kind of document it is.
 *
 * A prefix rather than the whole file, and the bound is what makes the test meaningful: a
 * research report carries its byline on the third line, so a document that mentions the
 * writer's name a thousand lines down is QUOTING a report, not being one. Reading everything
 * would turn a quotation into a misclassification.
 *
 * @param path Where.
 * @param out  Receives the head.
 * @return false when the file could not be opened.
 */
[[nodiscard]] bool readHead(const std::string &path, std::string &out)
{
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return false;
    char chunk[4096];
    const std::size_t read = std::fread(chunk, 1u, sizeof(chunk), file);
    out.assign(chunk, read);
    return std::fclose(file) == 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0)
    {
        lpl::apps::printIdentity(stdout, "lpl-ingest");
        return 0;
    }

    // --store is GONE, and its absence is the design rather than a removal. Its job was to
    // gather scattered documents into one place; the author gathered them by MOVING them, so a
    // copy would only be a second version of a file one directory away. What still lives
    // elsewhere — a CLAUDE.md loaded from a project root, a README rendered where it sits, the
    // book split from its own path — is read in place, which is what this tool does anyway.
    bool strict = false;
    bool includeStore = false;
    bool withText = false;
    bool withMentions = false;
    const char *hathiFile = nullptr;
    const char *gutenbergRoot = nullptr;
    const char *oaiEndpoint = nullptr;
    const char *oaiSet = nullptr;
    const char *oaiFrom = nullptr;
    lpl::core::u32 oaiMaxPages = 0u;
    bool streamed = false;
    lpl::core::u64 partCeiling = lpl::harvest::CatalogueStream::kImageCeiling;
    int first = 1;
    while (first < argc && argv[first][0] == '-' && argv[first][1] == '-')
    {
        if (std::strcmp(argv[first], "--strict") == 0)
        {
            strict = true;
            ++first;
        }
        else if (std::strcmp(argv[first], "--mentions") == 0)
            withMentions = true, ++first;
        else if (std::strcmp(argv[first], "--with-text") == 0)
        {
            withText = true;
            ++first;
        }
        else if (std::strcmp(argv[first], "--max-part") == 0 && first + 1 < argc)
        {
            // Useful for more than testing: a part sized to a medium — a disc, a kernel-carried
            // blob — is how a catalogue gets somewhere that is not this machine.
            partCeiling = static_cast<lpl::core::u64>(std::strtoull(argv[first + 1], nullptr, 10));
            first += 2;
        }
        else if (std::strcmp(argv[first], "--stream") == 0)
        {
            streamed = true;
            ++first;
        }
        else if (std::strcmp(argv[first], "--hathi") == 0 && first + 1 < argc)
        {
            hathiFile = argv[first + 1];
            first += 2;
        }
        else if (std::strcmp(argv[first], "--gutenberg") == 0 && first + 1 < argc)
        {
            gutenbergRoot = argv[first + 1];
            first += 2;
        }
        else if (std::strcmp(argv[first], "--oai") == 0 && first + 1 < argc)
        {
            oaiEndpoint = argv[first + 1];
            first += 2;
        }
        else if (std::strcmp(argv[first], "--oai-set") == 0 && first + 1 < argc)
        {
            oaiSet = argv[first + 1];
            first += 2;
        }
        else if (std::strcmp(argv[first], "--oai-from") == 0 && first + 1 < argc)
        {
            // @warning This IS incremental synchronisation. "Everything that changed since I last
            // looked" is a parameter of OAI-PMH, so carrying the previous run's newest
            // datestamp forward is the whole mechanism — there is nothing else to build.
            oaiFrom = argv[first + 1];
            first += 2;
        }
        else if (std::strcmp(argv[first], "--oai-pages") == 0 && first + 1 < argc)
        {
            oaiMaxPages = static_cast<lpl::core::u32>(std::strtoul(argv[first + 1], nullptr, 10));
            first += 2;
        }
        else if (std::strcmp(argv[first], "--include-store") == 0)
        {
            includeStore = true;
            ++first;
        }
        else
        {
            usage();
            return 2;
        }
    }
    // A catalogue bake needs no corpus root, so one argument is enough when --hathi or --oai
    // is given: the holdings come from a file or from a repository, not from a directory tree.
    const bool catalogueOnly = hathiFile != nullptr || oaiEndpoint != nullptr || gutenbergRoot != nullptr;
    // @warning A repository harvest is UNBOUNDED — arXiv alone answers 1300 records a page and does
    // not say how many pages — so streaming is the only shape it can take. Implied rather than
    // demanded: `--oai <url> out.lplknow` is unambiguous about what it wants, and refusing it
    // to make a flag be typed teaches the caller nothing. Without this the run fell through to
    // the in-memory path and baked an EMPTY image, silently.
    if (oaiEndpoint != nullptr || gutenbergRoot != nullptr)
        streamed = true;
    if (argc < first + (catalogueOnly ? 1 : 2))
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
        if (!collect(root, root.filename().string(), sources, includeStore))
        {
            // A flag in the roots position is the mistake a caller actually makes, and
            // "--with-text is not a directory" sends them looking for a missing folder.
            if (argv[i][0] == '-' && argv[i][1] == '-')
                std::fprintf(stderr, "lpl-ingest: %s is a flag, and flags come before the output path\n",
                             argv[i]);
            else
                std::fprintf(stderr, "lpl-ingest: %s is not a directory\n", argv[i]);
            return 1;
        }
    }

    if (sources.empty() && !catalogueOnly)
    {
        std::fprintf(stderr, "lpl-ingest: no markdown found under the given roots\n");
        return 1;
    }

    // One document, one reader. A research report is markdown too, so without this split it
    // would be read as a pile of prose with no stable identifiers in it — which is exactly
    // what a real run measured: eleven kilobytes, seven sources, twenty findings, and an
    // image asserting nothing at all. The dispatch is on the writer's byline; see
    // lpl::harvest::looksLikeResearchReport for why content is safe to key on here.
    std::vector<lpl::harvest::ResearchDocument> reports;
    std::vector<lpl::harvest::TeiSource> tei;
    std::vector<lpl::harvest::MarkdownSource> plain;
    plain.reserve(sources.size());
    for (lpl::harvest::MarkdownSource &source : sources)
    {
        std::string head;
        if (!readHead(source.path, head))
        {
            std::fprintf(stderr, "lpl-ingest: cannot read %s\n", source.path.c_str());
            return 1;
        }
        if (lpl::harvest::looksLikeTei(head))
            tei.push_back(lpl::harvest::TeiSource{source.path, source.canonical});
        else if (lpl::harvest::looksLikeResearchReport(head))
            reports.push_back(lpl::harvest::ResearchDocument{source.path, source.canonical});
        else if (source.path.size() > 3u && source.path.compare(source.path.size() - 3u, 3u, ".md") == 0)
            plain.push_back(std::move(source));
    }

    lpl::harvest::Baker baker;
    lpl::harvest::IngestReport report{};
    if (!plain.empty() && !lpl::harvest::ingestMarkdown(plain, baker, report))
    {
        std::fprintf(stderr, "lpl-ingest: a document could not be read, or two names collided\n");
        return 1;
    }

    lpl::harvest::ResearchIngestReport research{};
    if (!reports.empty() && !lpl::harvest::ingestResearchReports(reports, baker, research))
    {
        std::fprintf(stderr, "lpl-ingest: a research report could not be read, or two names collided\n");
        return 1;
    }

    // The catalogue half: holdings rather than facts. Read before the roots are walked so a
    // run may be a pure catalogue bake with no corpus directory at all.
    // ── The streamed path, for a catalogue larger than memory ────────────────
    // Separate and early, because it writes the image itself rather than handing bytes to the
    // in-memory baker — which is the whole point. It carries holdings only: a stream cannot
    // sort, and facts are sorted into a canonical order so two machines bake the same bytes.
    if (streamed)
    {
        if (hathiFile == nullptr && oaiEndpoint == nullptr && gutenbergRoot == nullptr)
        {
            std::fprintf(stderr,
                         "lpl-ingest: --stream needs --hathi, --oai or --gutenberg; it bakes holdings only\n");
            return 2;
        }
        lpl::harvest::CatalogueStream stream;
        if (!stream.open(output, partCeiling))
        {
            std::fprintf(stderr, "lpl-ingest: cannot open %s for streaming\n", output);
            return 1;
        }

        lpl::harvest::CatalogueIngestReport streamReport{};
        lpl::harvest::OaiHarvestReport oaiReport{};

        if (gutenbergRoot != nullptr)
        {
            // @warning One file per work, so the catalogue IS a directory tree — which is why the
            // reader took a list of paths from the day it was written and had, until now, no
            // caller able to build one. Collected before the bake rather than streamed from the
            // walk: the count is wanted in the report, and 79 000 paths is five megabytes.
            std::vector<std::string> rdf;
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator
                     it{std::filesystem::path{gutenbergRoot},
                        std::filesystem::directory_options::skip_permission_denied, error},
                 end;
                 it != end; it.increment(error))
            {
                if (error)
                    break;
                if (it->is_regular_file(error) && it->path().extension() == ".rdf")
                    rdf.push_back(it->path().string());
            }
            if (rdf.empty())
            {
                std::fprintf(stderr, "lpl-ingest: no .rdf found under %s\n", gutenbergRoot);
                return 1;
            }
            // Sorted, so that two runs over the same tree bake the same bytes. A directory walk
            // returns entries in whatever order the filesystem holds them, which is not an order.
            std::sort(rdf.begin(), rdf.end());
            std::printf("reading %zu Gutenberg records\n", rdf.size());

            const lpl::core::u32 holder = lpl::corpus::nameIdentifier(
                "ProjectGutenberg", static_cast<lpl::core::u32>(std::strlen("ProjectGutenberg")));
            if (!stream.name(holder, "Project Gutenberg") ||
                !lpl::harvest::ingestGutenbergRdf(rdf, holder, stream, streamReport))
            {
                std::fprintf(stderr, "lpl-ingest: cannot read the Gutenberg catalogue under %s\n",
                             gutenbergRoot);
                return 1;
            }
        }
        else if (oaiEndpoint != nullptr)
        {
            // The repository is named by its endpoint, because that is the only name it gives
            // itself that is stable — `<repositoryName>` is prose an administrator can change.
            const lpl::core::u32 holder = lpl::corpus::nameIdentifier(
                oaiEndpoint, static_cast<lpl::core::u32>(std::strlen(oaiEndpoint)));
            lpl::harvest::OaiRequest request;
            request.endpoint = oaiEndpoint;
            if (oaiSet != nullptr)
                request.set = oaiSet;
            if (oaiFrom != nullptr)
                request.from = oaiFrom;

            lpl::harvest::CurlFetcher fetcher;
            if (!stream.name(holder, oaiEndpoint) ||
                !lpl::harvest::harvestOaiPmh(fetcher, request, stream, holder, oaiReport, oaiMaxPages))
            {
                std::fprintf(stderr, "lpl-ingest: the harvest of %s stopped — %s\n", oaiEndpoint,
                             oaiReport.failure.empty() ? "no reason given" : oaiReport.failure.c_str());
                return 1;
            }
        }
        else
        {
            const lpl::core::u32 holder = lpl::corpus::nameIdentifier(
                "HathiTrust", static_cast<lpl::core::u32>(std::strlen("HathiTrust")));
            if (!stream.name(holder, "HathiTrust") ||
                !lpl::harvest::ingestHathiFile(hathiFile, holder, stream, streamReport))
            {
                std::fprintf(stderr, "lpl-ingest: cannot read the holdings file %s\n", hathiFile);
                return 1;
            }
        }

        lpl::harvest::BakeReport streamBake{};
        if (!stream.finish(streamBake))
        {
            std::fprintf(stderr, "lpl-ingest: the streamed bake failed (out of space, or past 4 GiB)\n");
            return 1;
        }
        if (oaiEndpoint != nullptr)
        {
            std::printf("harvested %u records over %u pages (%u deleted, %u untitled)\n", oaiReport.records,
                        oaiReport.pages, oaiReport.deleted, oaiReport.untitled);
            if (oaiReport.retries != 0u)
                std::printf("  waited %u times because the repository asked\n", oaiReport.retries);
            // @warning Printed because it is the input to the NEXT run. Without it, an incremental
            // harvest has no way to say where it got to, and every run is a full one.
            if (!oaiReport.lastDatestamp.empty())
                std::printf("  next run: --oai-from %s\n", oaiReport.lastDatestamp.c_str());
            if (!oaiReport.lastToken.empty())
                std::printf("  stopped holding token %s\n", oaiReport.lastToken.c_str());
        }
        else
        {
            std::printf("streamed %u catalogue rows (%u malformed)\n", streamReport.rows,
                        streamReport.malformed);
            std::printf("  %u public domain, %u full view, %u published before 1900\n",
                        streamReport.publicDomain, streamReport.fullView, streamReport.beforeNineteenHundred);
        }
        // Parts, when the holdings did not fit one image. Named rather than merely counted:
        // a caller that gets three files back needs to know which three.
        if (stream.parts() > 1u)
        {
            std::printf("split into %u parts (the format addresses 4 GiB per image):\n", stream.parts());
            for (lpl::core::u32 i = 0u; i < stream.parts(); ++i)
                std::printf("  %s\n", stream.partPath(i).c_str());
            // A row index does not survive a part boundary; say how many relations were lost
            // rather than letting the number quietly go missing.
            if (stream.clustersDropped() != 0u)
                std::printf("  %u rows lost their work to a boundary (raise --max-part to keep them)\n",
                            stream.clustersDropped());
        }
        else
        {
            std::printf("wrote %s - %u bytes: %u text lines, %u names, %u catalogue rows\n", output,
                        streamBake.bytes, streamBake.textLines, streamBake.vocabulary, streamBake.catalogue);
        }
        return 0;
    }

    lpl::harvest::CatalogueIngestReport hathiReport{};
    lpl::harvest::CatalogueResolution resolution{};
    if (hathiFile != nullptr)
    {
        const lpl::core::u32 holder =
            lpl::corpus::nameIdentifier("HathiTrust", static_cast<lpl::core::u32>(std::strlen("HathiTrust")));
        std::vector<std::string> keys;
        if (!baker.name(holder, "HathiTrust") ||
            !lpl::harvest::ingestHathiFile(hathiFile, holder, baker, hathiReport, &keys))
        {
            std::fprintf(stderr, "lpl-ingest: cannot read the holdings file %s\n", hathiFile);
            return 1;
        }
        // Which of those holdings are copies of one work. Only on the in-memory path: it needs
        // every key at once, which is exactly what the streamed path exists not to do.
        if (!lpl::harvest::resolveCatalogue(baker.catalogue(), keys, resolution))
        {
            std::fprintf(stderr, "lpl-ingest: the catalogue and its keys disagree in length\n");
            return 1;
        }
    }

    lpl::harvest::TeiIngestReport teiReport{};
    if (!tei.empty())
    {
        lpl::harvest::TeiOptions options;
        options.carryText = withText;
        options.carryMentions = withMentions;
        if (!lpl::harvest::ingestTei(tei, options, baker, teiReport))
        {
            if (!teiReport.firstCollision.empty())
                std::fprintf(stderr, "lpl-ingest: one identifier is claimed twice - %s\n",
                             teiReport.firstCollision.c_str());
            else
                std::fprintf(stderr, "lpl-ingest: a TEI document could not be read\n");
            return 1;
        }
    }

    std::printf("read %u documents, %u lines, %u headings (%u dated)\n", report.documents, report.lines,
                report.headings, report.dated);
    std::printf("  %u identifiers defined, %u citations\n", report.definitions, report.citations);
    if (report.footnotes != 0u || report.footnoteCitations != 0u)
        std::printf("  %u footnotes defined, %u references\n", report.footnotes, report.footnoteCitations);
    if (report.footnoteNoise != 0u)
        std::printf("  %u footnote markers are prose, not references\n", report.footnoteNoise);
    if (report.danglingFootnotes != 0u)
        std::printf("  /!\\ %u footnote references resolve to nothing\n", report.danglingFootnotes);
    if (research.reports != 0u)
    {
        std::printf("  %u research reports, %u lines (%u dated)\n", research.reports, research.lines,
                    research.dated);
        std::printf("    %u findings, %u sources (%u confirmed reachable, %u unreadable), %u inline citations\n",
                    research.findings, research.sources, research.reachable, research.unreadable,
                    research.citations);
        // How much of the agreement in this corpus is real. `collapsed` counts rows that
        // turned out to be another spelling of a source already seen; `inflated` counts
        // findings that several rows assert and one witness attests. Printed together with
        // the corpus they were measured on, because a zero here means either no duplication
        // or no detection and the two read identically.
        std::printf("    %u independent witnesses (%u rows collapsed), %u findings corroborated, "
                    "%u only apparently\n",
                    research.witnesses, research.collapsed, research.corroborated, research.inflated);
        if (research.orphanFindings != 0u)
            std::printf("    /!\\ %u findings cite a source the report does not list - first: %s\n",
                        research.orphanFindings, research.firstOrphan.c_str());
    }
    if (teiReport.documents != 0u)
    {
        std::printf("  %u TEI documents, %u works, %u authors\n", teiReport.documents, teiReport.works,
                    teiReport.authors);
        std::printf("    %u citable passages, deepest nesting %u level(s)\n", teiReport.passages,
                    teiReport.deepest);
        if (teiReport.tooDeep != 0u)
            std::printf("    %u nested deeper than a three-ordinal locus can hold\n", teiReport.tooDeep);
        if (teiReport.unnumbered != 0u)
            std::printf("    %u divisions carry no number at all (prefaces, arguments)\n",
                        teiReport.unnumbered);
        if (teiReport.textLines != 0u)
            std::printf("    %u passages carried verbatim (%u truncated)\n", teiReport.textLines,
                        teiReport.truncated);
        if (teiReport.placeMentions != 0u || teiReport.personMentions != 0u || teiReport.datedPassages != 0u)
        {
            std::printf("    %u place mentions (%u without an authority key), %u person mentions\n",
                        teiReport.placeMentions, teiReport.unkeyedPlaces, teiReport.personMentions);
            std::printf("    %u passages carry a date the editor marked up\n", teiReport.datedPassages);
        }
        // Counted rather than hidden: a citation this scheme cannot express is a real gap in
        // the index, and a reader that silently invented an ordinal would print a reference
        // that is not the one in the book.
        if (teiReport.unaddressable != 0u)
            std::printf("    %u passages have a citation this scheme cannot express - first: %s\n",
                        teiReport.unaddressable, teiReport.firstUnaddressable.c_str());
        if (teiReport.withoutUrn != 0u)
            std::printf("    %u works name no CTS URN, identified by path\n", teiReport.withoutUrn);
    }
    if (hathiReport.rows != 0u || hathiReport.malformed != 0u)
    {
        std::printf("  %u catalogue rows (%u malformed)\n", hathiReport.rows, hathiReport.malformed);
        std::printf("    %u public domain, %u full view, %u published before 1900\n",
                    hathiReport.publicDomain, hathiReport.fullView, hathiReport.beforeNineteenHundred);
        std::printf("    %u without a title, %u without a creator, %u without a year\n",
                    hathiReport.withoutTitle, hathiReport.withoutCreator, hathiReport.withoutYear);
        // @warning `unkeyed` is reported apart from `clusters` because a row with no bibliographic
        // number is UNRESOLVED, which is a different thing from a work held once.
        std::printf("    %u distinct works, %u copies folded, %u unkeyed, largest held %u times\n",
                    resolution.clusters, resolution.merged, resolution.unkeyed, resolution.largest);
    }
    if (report.dangling != 0u)
        std::printf("  /!\\ %u citations resolve to nothing - first: %s\n", report.dangling,
                    report.firstDangling.c_str());
    if (report.duplicates != 0u)
        std::printf("  /!\\ %u identifiers defined twice - first: %s\n", report.duplicates,
                    report.firstDuplicate.c_str());

    if (strict && (report.dangling != 0u || report.duplicates != 0u || research.orphanFindings != 0u))
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

    std::printf("wrote %s - %u bytes: %u facts, %u sources, %u documents, %u loci, %u names, %u text lines",
                output, bake.bytes, bake.facts, bake.sources, bake.documents, bake.loci, bake.vocabulary,
                bake.textLines);
    if (bake.catalogue != 0u)
        std::printf(", %u catalogue rows", bake.catalogue);
    std::printf("\n");
    return 0;
}
