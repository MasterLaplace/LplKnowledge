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
 * ⚠ The caller-side sketch here named `lpl::graph::Bayes::combine` and
 * `lpl::graph::Trust::standard()`. Both exist, under other names, in the module that owns the
 * arithmetic: `history::fuseConfidence` and `history::TrustWeights{}`, and the consensus a
 * whole corpus reaches is `history::buildTimeline` with an all-listening `WorldView`. The
 * shape of the sketch survived; only the namespace was wrong, because it was written before
 * `history/` existed. See graph/FOLDED.md.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include <lpl/Foundation.hpp>

#include <lpl/corpus/TextView.hpp>
#include <lpl/corpus/Urn.hpp>
#include <lpl/harvest/Markdown.hpp>
#include <lpl/knowledge/FactStore.hpp>
#include <lpl/knowledge/KnowledgePack.hpp>
#include <lpl/knowledge/Provenance.hpp>
#include <lpl/knowledge/Query.hpp>

#if defined(LPL_HAS_FOUNDATION)
#    include <lpl/history/PossibleWorld.hpp>
#    include <lpl/knowledge/History.hpp>
#endif

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
                         "\n"
                         "Identifiers are the words an image carries; pass them as numbers and the\n"
                         "vocabulary resolves them back for display. With no term, prints what the\n"
                         "image contains — which is the first question anyone has about a file they\n"
                         "were handed.\n"
                         "\n"
                         "  --define  takes the identifier BY NAME and prints what it means, where it\n"
                         "            is defined and who cites it. The definition comes out of the\n"
                         "            image, not out of the document — which is what makes an index a\n"
                         "            rendered view instead of a table somebody maintains.\n");
}

/**
 * @brief Reads a whole file.
 *
 * @param path Where.
 * @param out  Receives the bytes.
 * @return false when the file could not be read.
 */
[[nodiscard]] bool readFile(const char *path, std::vector<lpl::core::u8> &out)
{
    std::FILE *file = std::fopen(path, "rb");
    if (file == nullptr)
        return false;
    lpl::core::u8 chunk[4096];
    std::size_t read = 0u;
    while ((read = std::fread(chunk, 1u, sizeof(chunk), file)) > 0u)
        out.insert(out.end(), chunk, chunk + read);
    return std::fclose(file) == 0;
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

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        usage();
        return 2;
    }

    std::vector<lpl::core::u8> image;
    if (!readFile(argv[1], image))
    {
        std::fprintf(stderr, "lpl-ask: cannot read %s\n", argv[1]);
        return 1;
    }

    lpl::knowledge::KnowledgePack pack;
    const lpl::knowledge::OpenStatus status = pack.open(image.data(), static_cast<lpl::core::u32>(image.size()));
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

    lpl::knowledge::ProvenanceAudit audit{};
    if (!lpl::knowledge::auditProvenance(pack, audit))
        std::printf("  ⚠ provenance: %u unsourced, %u dangling loci, %u dangling documents\n", audit.unsourced,
                    audit.danglingLocus, audit.danglingDocument);

    char described[192];
    (void) lpl::knowledge::describeQuery(query, described, sizeof(described));
    std::printf("\nasking: %s\n", described);

    const lpl::knowledge::FactStore store{pack};
    lpl::knowledge::Page page;
    store.run(query, page);

    for (lpl::core::u32 i = 0u; i < page.count; ++i)
    {
        const lpl::knowledge::FactV1 &row = page.rows[i];
        char subject[96];
        char predicate[96];
        char object[96];
        std::printf("  %s %s %s  [%d..%d]  sigma_raw=%u\n", display(pack, row.subject, subject, sizeof(subject)),
                    display(pack, row.predicate, predicate, sizeof(predicate)),
                    display(pack, row.object, object, sizeof(object)), row.fromYear, row.toYear, row.confidenceRaw);

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

#if defined(LPL_HAS_FOUNDATION)
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
        // ⚠ Said rather than implied: the mutual-exclusion rule assumes a FUNCTIONAL
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
#else
    std::printf("consensus: unavailable in a standalone build — the arithmetic of doubt is\n"
                "           lpl::history, and Fixed32 is not emulated here\n");
#endif

    return 0;
}
