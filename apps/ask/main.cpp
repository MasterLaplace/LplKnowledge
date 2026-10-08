/**
 * @file main.cpp
 * @brief Query the knowledge pack from a terminal.
 *
 * Structured filter first, similarity second. A relational predicate removes most
 * of the corpus before any embedding is consulted, and a join — not a language
 * model — supplies the linked facts, so nothing can invent a connection that was
 * never recorded. The same reflex as authoritative-integer, cosmetic-float,
 * applied to retrieval.
 *
 * Today there is no similarity half, and that is stated rather than stubbed: the structured
 * filter is real and the vector index arrives with the corpus that needs it. What matters is
 * that the ORDER is already the right way round — an embedding added later narrows what SQL
 * left, and never the reverse.
 *
 * @warning The caller-side sketch here named `lpl::graph::Bayes::combine` and
 * `lpl::graph::Trust::standard()`. Both exist, under other names, in the module that owns the
 * arithmetic: `history::fuseConfidence` and `history::TrustWeights{}`, and the consensus a
 * whole corpus reaches is `history::buildTimeline` with an all-listening `WorldView`. The
 * shape of the sketch survived; only the namespace was wrong, because it was written before
 * `history/` existed. See graph/FOLDED.md.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include "Identity.hpp"

#include <lpl/Foundation.hpp>

#include <lpl/corpus/TextView.hpp>
#include <lpl/corpus/Urn.hpp>
#include <lpl/harvest/CatalogueStream.hpp>
#include <lpl/harvest/MappedFile.hpp>
#include <lpl/harvest/Markdown.hpp>
#include <lpl/harvest/Predicates.hpp>
#include <lpl/knowledge/FactStore.hpp>
#include <lpl/knowledge/KnowledgePack.hpp>
#include <lpl/knowledge/Provenance.hpp>
#include <lpl/knowledge/Query.hpp>

#include <lpl/history/PossibleWorld.hpp>
#include <lpl/knowledge/History.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

/**
 * @brief Prints how to use the tool.
 */
void usage()
{
    std::fprintf(stderr, "usage: lpl-ask <image.lplknow> [--subject N] [--predicate N] [--object N]\n"
                         "                              [--source N] [--year Y] [--limit N]\n"
                         "\n"
                         "       lpl-ask <image.lplknow> --define <IDENTIFIER>\n"
                         "       lpl-ask <image.lplknow> [more.lplknow ...] --holdings\n"
                         "                               [--year-from Y] [--year-to Y]\n"
                         "                               [--public-domain] [--full-view] [--transcribed]\n"
                         "                               [--language N] [--holder N] [--title SUBSTR]\n"
                         "\n"
                         "Identifiers are the words an image carries; pass them as numbers and the\n"
                         "vocabulary resolves them back for display. With no term, prints what the\n"
                         "image contains — which is the first question anyone has about a file they\n"
                         "were handed.\n"
                         "\n"
                         "  --define  takes the identifier BY NAME and prints what it means, where it\n"
                         "            is defined and who cites it. The definition comes out of the\n"
                         "            image, not out of the document — which is what makes an index a\n"
                         "            rendered view instead of a table somebody maintains.\n"
                         "  --version  print the version, commit and build of this tool, and the\n"
                         "            LplPlugin it was built with.\n");
}

/**
 * @brief What an identifier reads as, or its number when the image does not name it.
 *
 * @param pack The image.
 * @param id   The identifier.
 * @param out  Receives a display string.
 * @param cap  Room in @p out.
 * @return @p out.
 */
const char *display(const lpl::knowledge::KnowledgePack &pack, lpl::core::u32 id, char *out, std::size_t cap)
{
    if (const char *text = pack.textFor(id); text != nullptr)
    {
        std::snprintf(out, cap, "%s", text);
        return out;
    }
    std::snprintf(out, cap, "#%u", id);
    return out;
}

/**
 * @brief Prints what one identifier means, where it is defined, and who cites it.
 *
 * @param pack The image.
 * @param name The identifier, as a word.
 * @return 0 when the identifier is known, 1 when nothing defines it.
 */
int describe(const lpl::knowledge::KnowledgePack &pack, const char *name)
{
    const lpl::core::u32 subject =
        lpl::corpus::nameIdentifier(name, static_cast<lpl::core::u32>(std::strlen(name)));
    const lpl::knowledge::FactStore store{pack};

    lpl::knowledge::Page page;
    lpl::knowledge::Query words;
    words.about(subject).asserting(lpl::harvest::kPredicateDefinitionText);
    store.run(words, page);

    if (page.count == 0u)
    {
        std::printf("%s: nothing in this image defines it\n", name);
        return 1;
    }

    const lpl::core::u8 *section = nullptr;
    lpl::core::u32 sectionBytes = 0u;
    lpl::corpus::TextView text;
    const bool haveText = pack.section(lpl::knowledge::SectionType::Texts, section, sectionBytes) &&
                          text.open(section, sectionBytes);

    std::printf("%s\n", name);
    if (haveText)
    {
        const char *line = nullptr;
        lpl::core::u32 size = 0u;
        if (text.line(page.rows[0].object, line, size))
            std::printf("  %.*s\n", static_cast<int>(size), line);
    }
    else
    {
        // Said rather than left blank: an image baked without its text section is a
        // legitimate image — a ring-0 reader has no business carrying prose — and a silent
        // empty line would read as an identifier with no definition.
        std::printf("  (this image carries no text section)\n");
    }

    lpl::knowledge::Query definition;
    definition.about(subject).asserting(lpl::harvest::kPredicateDefinedIn);
    store.run(definition, page);
    for (lpl::core::u32 i = 0u; i < page.count; ++i)
    {
        lpl::knowledge::Citation citation{};
        if (!lpl::knowledge::cite(pack, page.rows[i], citation))
            continue;
        char rendered[192];
        (void) lpl::knowledge::renderCitation(citation, rendered, sizeof(rendered));
        std::printf("  defined in %s\n", rendered);
    }

    lpl::knowledge::Query cited;
    cited.about(subject).asserting(lpl::harvest::kPredicateCitedIn).take(lpl::knowledge::kMaxPageRows);
    store.run(cited, page);
    std::printf("  cited %u time%s\n", page.matched, page.matched == 1u ? "" : "s");
    for (lpl::core::u32 i = 0u; i < page.count; ++i)
    {
        lpl::knowledge::Citation citation{};
        if (!lpl::knowledge::cite(pack, page.rows[i], citation))
            continue;
        char rendered[192];
        (void) lpl::knowledge::renderCitation(citation, rendered, sizeof(rendered));
        std::printf("    %s\n", rendered);
    }
    if (page.truncated != 0u)
        std::printf("    ... and %u more\n", page.matched - page.count);
    return 0;
}

} // namespace

/**
 * @brief Walks a catalogue, printing what matches.
 *
 * A scan, and deliberately so: a catalogue is written once and read whole, and an index over
 * nineteen million rows would be a second structure to keep true. The mapping makes the scan
 * affordable — pages are read as they are touched and dropped behind, so resident memory stays
 * flat whatever the file weighs.
 *
 * @param pack The image.
 * @param argc Argument count.
 * @param argv Arguments.
 * @return 0 on success.
 */
/**
 * @struct BrowseTally
 * @brief Running totals across however many images the question was asked of.
 */
struct BrowseTally {
    lpl::core::u32 matched{0u};
    lpl::core::u32 shown{0u};
    lpl::core::u32 total{0u};
};

/**
 * @brief Walks one image's holdings.
 *
 * @param pack       The image.
 * @param query      The integer terms.
 * @param titleNeedle Substring the title must contain, or nullptr.
 * @param limit      How many rows to print in total, across every image.
 * @param tally      Carried between images.
 */
void browseOne(const lpl::knowledge::KnowledgePack &pack, const lpl::knowledge::CatalogueQuery &query,
               const char *titleNeedle, lpl::core::u32 limit, BrowseTally &tally)
{
    const lpl::core::u8 *section = nullptr;
    lpl::core::u32 sectionBytes = 0u;
    lpl::corpus::TextView texts;
    const bool haveTexts = pack.section(lpl::knowledge::SectionType::Texts, section, sectionBytes) &&
                           texts.open(section, sectionBytes);

    lpl::knowledge::CatalogueEntryV1 entry{};
    tally.total += pack.catalogueCount();

    for (lpl::core::u32 i = 0u; i < pack.catalogueCount(); ++i)
    {
        if (!pack.catalogueAt(i, entry) || !lpl::knowledge::matchesCatalogue(query, entry))
            continue;

        const char *title = nullptr;
        lpl::core::u32 titleBytes = 0u;
        // @warning One-based: zero means the holder named none, and rendering line zero instead would
        // credit this row with another row's words.
        const bool haveTitle = haveTexts && entry.title != lpl::knowledge::kNoIdentifier &&
                               texts.line(entry.title - 1u, title, titleBytes);

        // The substring filter is applied AFTER the integer terms, which is the same
        // filter-then-similarity order `lpl-ask` already follows: the cheap exact terms remove
        // most of the corpus before anything walks a string.
        if (titleNeedle != nullptr)
        {
            if (!haveTitle)
                continue;
            if (std::string_view{title, titleBytes}.find(titleNeedle) == std::string_view::npos)
                continue;
        }

        ++tally.matched;
        if (tally.shown >= limit)
            continue;
        ++tally.shown;

        const char *creator = nullptr;
        lpl::core::u32 creatorBytes = 0u;
        const bool haveCreator = haveTexts && entry.creator != lpl::knowledge::kNoIdentifier &&
                                 texts.line(entry.creator - 1u, creator, creatorBytes);
        const char *address = nullptr;
        lpl::core::u32 addressBytes = 0u;
        const bool haveAddress = haveTexts && entry.address != lpl::knowledge::kNoIdentifier &&
                                 texts.line(entry.address - 1u, address, addressBytes);

        std::printf("  %.*s\n", static_cast<int>(haveTitle ? titleBytes : 0u), haveTitle ? title : "");
        std::printf("    %.*s", static_cast<int>(haveCreator ? creatorBytes : 0u),
                    haveCreator ? creator : "");
        if (entry.year != 0)
        {
            // @warning The WINDOW when it is wider than a year, because that is what is known. A
            // holding dated only through its author spans that author's life -- Herodotus is
            // fifty-four years wide -- and printing one end of it presents a guess as a date.
            std::printf("%s%d", haveCreator && creatorBytes != 0u ? ", " : "", entry.year);
            if (entry.yearTo != 0 && entry.yearTo != entry.year)
                std::printf("..%d", entry.yearTo);
        }
        std::printf("%s%s\n", (entry.flags & lpl::knowledge::kCatalogueFlagPublicDomain) != 0u
                                  ? "  [domaine public]" : "",
                    (entry.flags & lpl::knowledge::kCatalogueFlagTranscription) != 0u ? "  [transcrit]"
                                                                                     : "");
        if (haveAddress)
            std::printf("    <%.*s>\n", static_cast<int>(addressBytes), address);
    }
}

[[nodiscard]] int browse(const std::vector<std::string> &paths, int argc, char **argv, int flagStart)
{
    lpl::knowledge::CatalogueQuery query;
    const char *titleNeedle = nullptr;

    for (int i = flagStart; i < argc; ++i)
    {
        const char *flag = argv[i];
        const bool hasValue = i + 1 < argc;
        const long value = hasValue ? std::strtol(argv[i + 1], nullptr, 10) : 0;
        if (std::strcmp(flag, "--public-domain") == 0)
            query.requiredFlags |= lpl::knowledge::kCatalogueFlagPublicDomain;
        else if (std::strcmp(flag, "--full-view") == 0)
            query.requiredFlags |= lpl::knowledge::kCatalogueFlagFullView;
        else if (std::strcmp(flag, "--transcribed") == 0)
            query.requiredFlags |= lpl::knowledge::kCatalogueFlagTranscription;
        else if (hasValue && std::strcmp(flag, "--year-from") == 0)
            query.fromDay = static_cast<lpl::core::i32>(value), ++i;
        else if (hasValue && std::strcmp(flag, "--year-to") == 0)
            query.toDay = static_cast<lpl::core::i32>(value), ++i;
        else if (hasValue && std::strcmp(flag, "--language") == 0)
            query.language = static_cast<lpl::core::u32>(value), ++i;
        else if (hasValue && std::strcmp(flag, "--holder") == 0)
            query.holder = static_cast<lpl::core::u32>(value), ++i;
        else if (hasValue && std::strcmp(flag, "--limit") == 0)
            query.limit = static_cast<lpl::core::u32>(value), ++i;
        else if (hasValue && std::strcmp(flag, "--title") == 0)
            titleNeedle = argv[i + 1], ++i;
    }

    // @warning One image is mapped at a time and unmapped before the next, which is what keeps a
    // question over a catalogue larger than memory a QUERY rather than a load. Mapping them all
    // at once would work on this machine and stop working at the size the parts exist for.
    BrowseTally tally;
    lpl::core::u32 visited = 0u;

    for (const std::string &path : paths)
    {
        lpl::harvest::MappedFile mapping;
        if (!mapping.open(path))
        {
            std::fprintf(stderr, "lpl-ask: cannot read %s\n", path.c_str());
            return 1;
        }

        lpl::knowledge::KnowledgePack pack;
        const lpl::knowledge::OpenStatus status = pack.open(mapping.bytes(), mapping.size());
        if (status != lpl::knowledge::OpenStatus::Ok)
        {
            std::fprintf(stderr, "lpl-ask: %s is not usable — %s\n", path.c_str(),
                         lpl::knowledge::openStatusText(status));
            return 1;
        }

        ++visited;
        browseOne(pack, query, titleNeedle, query.limit, tally);
    }

    std::printf("\n%u of %u holdings matched, %u shown", tally.matched, tally.total, tally.shown);
    // Said out loud, because the alternative is a total whose scope the reader has to guess: a
    // count over three files and a count over one look identical on the page.
    // @warning "images", not "parts". Several files here can be the parts of one split catalogue OR
    // four unrelated repositories asked at once — the tool cannot tell, and naming them parts
    // would assert the one it does not know.
    if (visited > 1u)
        std::printf(" (across %u images)", visited);
    std::printf("\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0)
    {
        lpl::apps::printIdentity(stdout, "lpl-ask");
        return 0;
    }

    if (argc < 2)
    {
        usage();
        return 2;
    }

    // ── Holdings ──────────────────────────────────────────────────────────────
    // @warning A catalogue too large for one image is written as several, so a question about holdings
    // is asked of a SET of files — and this mode is found BEFORE anything is opened, because
    // which files there are is part of the question. A whole mode rather than another filter on
    // `Query`, for the reason `CatalogueQuery` gives: a holding claims nothing, so it has no
    // subject, no source and no confidence to filter on.
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--holdings") != 0)
            continue;
        if (i < 2)
        {
            usage();
            return 2;
        }

        std::vector<std::string> paths;
        for (int p = 1; p < i; ++p)
            paths.emplace_back(argv[p]);

        // One path given: follow the writer's own convention to its siblings, so that the usual
        // invocation keeps working when a bake happens to split.
        if (paths.size() == 1u)
        {
            const std::string base = paths.front();
            lpl::core::u32 part = 1u;
            for (;; ++part)
            {
                const std::string sibling = lpl::harvest::cataloguePartPath(base, part);
                lpl::harvest::MappedFile probe;
                if (!probe.open(sibling))
                    break;
                paths.push_back(sibling);
            }

            // @warning **A gap is REFUSED, never walked past and never quietly stopped at.** Measured:
            // deleting one part of a four-part set made this tool answer "2 of 2 holdings" — a
            // true sentence about the wrong set, with nothing on the page to say so, which is
            // the exact silent-wrongness the rest of this module is built to prevent. Stopping
            // at the first gap is not safe on its own; it looks identical to a complete run. So
            // the sequence is probed a little past its end, and a part found beyond a missing
            // one means the set has a hole in it.
            constexpr lpl::core::u32 kGapProbe = 4u;
            for (lpl::core::u32 ahead = 1u; ahead <= kGapProbe; ++ahead)
            {
                lpl::harvest::MappedFile probe;
                if (!probe.open(lpl::harvest::cataloguePartPath(base, part + ahead)))
                    continue;
                std::fprintf(stderr,
                             "lpl-ask: %s is missing — the set continues at part %u, so an answer\n"
                             "         here would be over a catalogue with a hole in it\n",
                             lpl::harvest::cataloguePartPath(base, part).c_str(), part + ahead);
                return 1;
            }
        }

        return browse(paths, argc, argv, i + 1);
    }

    // @warning Mapped, never read. The streamed bake brought a nineteen-million-row catalogue down to
    // 7 MB of memory to WRITE, and this tool then spent 4.2 GB to open the 3.71 GB image it
    // produced — which leaves the ceiling exactly where it was, one process further along.
    // `KnowledgePack::open` takes a pointer and never copies, so a mapping is all it wants.
    lpl::harvest::MappedFile mapping;
    if (!mapping.open(argv[1]))
    {
        std::fprintf(stderr, "lpl-ask: cannot read %s (or it is past the format's 4 GiB ceiling)\n", argv[1]);
        return 1;
    }


    lpl::knowledge::KnowledgePack pack;
    const lpl::knowledge::OpenStatus status = pack.open(mapping.bytes(), mapping.size());
    if (status != lpl::knowledge::OpenStatus::Ok)
    {
        // The reason, not just a refusal: "this is not a knowledge image" and "this is a
        // knowledge image that has been damaged" send a reader looking in different places.
        std::fprintf(stderr, "lpl-ask: %s is not usable — %s\n", argv[1], lpl::knowledge::openStatusText(status));
        return 1;
    }

    // --define is a whole mode rather than another filter: it asks a different question
    // ("what does this mean, and who leans on it") and answers it with three queries and the
    // text section. Folding it into the filter loop would have made the output depend on
    // which flags happened to be combined.
    if (argc >= 4 && std::strcmp(argv[2], "--define") == 0)
        return describe(pack, argv[3]);

    lpl::knowledge::Query query;
    for (int i = 2; i + 1 < argc; i += 2)
    {
        const char *flag = argv[i];
        const long value = std::strtol(argv[i + 1], nullptr, 10);
        if (std::strcmp(flag, "--subject") == 0)
            query.about(static_cast<lpl::core::u32>(value));
        else if (std::strcmp(flag, "--predicate") == 0)
            query.asserting(static_cast<lpl::core::u32>(value));
        else if (std::strcmp(flag, "--object") == 0)
            query.object = static_cast<lpl::core::u32>(value);
        else if (std::strcmp(flag, "--source") == 0)
            query.accordingTo(static_cast<lpl::core::u32>(value));
        else if (std::strcmp(flag, "--year") == 0)
            query.during(static_cast<lpl::core::i32>(value));
        else if (std::strcmp(flag, "--limit") == 0)
            query.take(static_cast<lpl::core::u32>(value));
        else
        {
            std::fprintf(stderr, "lpl-ask: unknown option %s\n", flag);
            return 2;
        }
    }

    std::printf("%s — %u bytes, %u sections", argv[1], pack.size(), pack.sectionCount());
    if (pack.skippedSections() != 0u)
        std::printf(" (%u skipped, from a newer writer)", pack.skippedSections());
    std::printf("\n  %u facts · %u sources · %u documents · %u loci · %u names\n", pack.factCount(),
                pack.sourceCount(), pack.documentCount(), pack.locusCount(), pack.vocabularyCount());
    if (pack.catalogueCount() != 0u)
        // @warning Without this, an image of nineteen million holdings prints "0 facts, 0 documents"
        // and reads as empty. A catalogue asserts no claims by design, so the count is the only
        // thing that says it is there at all.
        std::printf("  %u catalogue rows\n", pack.catalogueCount());

    lpl::knowledge::ProvenanceAudit audit{};
    if (!lpl::knowledge::auditProvenance(pack, audit))
        std::printf("  @warning provenance: %u unsourced, %u dangling loci, %u dangling documents\n", audit.unsourced,
                    audit.danglingLocus, audit.danglingDocument);

    char described[192];
    (void) lpl::knowledge::describeQuery(query, described, sizeof(described));
    std::printf("\nasking: %s\n", described);

    const lpl::knowledge::FactStore store{pack};
    lpl::knowledge::Page page;
    store.run(query, page);

    // The Texts section, opened once for the whole page rather than per row. An image
    // legitimately has none — a ring-0 reader has no business carrying prose — so its absence
    // is a state to report, never a reason to print nothing.
    const lpl::core::u8 *textSection = nullptr;
    lpl::core::u32 textBytes = 0u;
    lpl::corpus::TextView texts;
    const bool haveTexts = pack.section(lpl::knowledge::SectionType::Texts, textSection, textBytes) &&
                           texts.open(textSection, textBytes);

    for (lpl::core::u32 i = 0u; i < page.count; ++i)
    {
        const lpl::knowledge::FactV1 &row = page.rows[i];
        char subject[96];
        char predicate[96];
        char object[96];

        // @warning An object is an identifier for most predicates and an INDEX INTO Texts for a
        // handful, and the wire record is the same 32 bits either way. Resolving the second
        // kind through the vocabulary is what printed `#1001` where a passage of Caesar
        // should have been. See harvest/Predicates.hpp for why the list lives in one place.
        const char *line = nullptr;
        lpl::core::u32 lineBytes = 0u;
        const bool words = lpl::harvest::objectIsTextLine(row.predicate) && haveTexts &&
                           texts.line(row.object, line, lineBytes);

        if (words)
            std::printf("  %s %s  [%d..%d]  sigma_raw=%u\n    \"%.*s\"\n",
                        display(pack, row.subject, subject, sizeof(subject)),
                        display(pack, row.predicate, predicate, sizeof(predicate)), row.fromDay, row.toDay,
                        row.confidenceRaw, static_cast<int>(lineBytes), line);
        else
            std::printf("  %s %s %s  [%d..%d]  sigma_raw=%u\n", display(pack, row.subject, subject, sizeof(subject)),
                        display(pack, row.predicate, predicate, sizeof(predicate)),
                        display(pack, row.object, object, sizeof(object)), row.fromDay, row.toDay,
                        row.confidenceRaw);

        lpl::knowledge::Citation citation{};
        if (lpl::knowledge::cite(pack, row, citation))
        {
            char rendered[192];
            (void) lpl::knowledge::renderCitation(citation, rendered, sizeof(rendered));
            std::printf("      %s\n", rendered);
        }
    }

    std::printf("\n%u matched, %u shown%s\n", page.matched, page.count,
                page.truncated != 0u ? " — capped; narrow the query rather than raising the cap" : "");

    // The consensus, from the module that owns the arithmetic. An all-listening WorldView is
    // what "the consensus world" means: same corpus, same function, no source excluded.
    lpl::history::Corpus corpus;
    lpl::knowledge::DecodeReport report{};
    if (lpl::knowledge::toHistoryCorpus(pack, corpus, report))
    {
        lpl::history::WorldView everyone;
        lpl::history::FusionReport fusion{};
        const lpl::history::Timeline timeline = lpl::history::buildTimeline(corpus, everyone, fusion);
        std::printf("timeline: %u constraints, %u fused, %u mutually-exclusive pairs, %u demoted\n",
                    timeline.size(), fusion.fused, fusion.contradictions, fusion.demoted);
        // @warning Said rather than implied: the mutual-exclusion rule assumes a FUNCTIONAL
        // predicate — one object per subject per window, as `history::contradicts` states —
        // and a document corpus has none. An identifier cited in twenty places trips the rule
        // twenty times over and nothing is contradicted. Printing "consensus" over such a
        // corpus would be printing a verdict where there is no question.
        if (fusion.contradictions != 0u && fusion.demoted == 0u)
            std::printf("          (flagged pairs, not disagreements: this corpus's predicates are\n"
                        "           many-valued, so the mutual-exclusion rule does not apply to it)\n");
    }
    else
    {
        std::printf("consensus: unavailable — %u records this build cannot read\n",
                    report.badKind + report.badWindow + report.badConfidence);
    }

    return 0;
}
