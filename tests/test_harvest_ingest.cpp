/**
 * @file test_harvest_ingest.cpp
 * @brief Reading a markdown corpus: what counts as an identifier, and what does not.
 *
 * The fixture is written to disk by the test rather than checked in, and that is the point:
 * what is under test is a READER, so the corpus has to be something the test controls down
 * to the line number. A checked-in fixture would drift the moment somebody reformatted it,
 * and the failure would look like a scanner bug.
 *
 * Needs no foundation: reading text into a baker is string handling, and the arithmetic of
 * doubt lives elsewhere.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include <lpl/harvest/Baker.hpp>
#include <lpl/harvest/Markdown.hpp>
#include <lpl/corpus/TextView.hpp>
#include <lpl/corpus/Urn.hpp>
#include <lpl/knowledge/FactStore.hpp>
#include <lpl/knowledge/KnowledgePack.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int gChecks = 0;
int gFailures = 0;

/**
 * @brief Records one check.
 *
 * @param label What was checked.
 * @param ok    Whether it held.
 */
void check(const char *label, bool ok)
{
    ++gChecks;
    if (!ok)
    {
        ++gFailures;
        std::printf("  (fail) %s\n", label);
    }
}

/**
 * @brief Writes a fixture document.
 *
 * @param path Where.
 * @param body What.
 */
void writeDocument(const std::filesystem::path &path, const char *body)
{
    std::ofstream file{path, std::ios::binary};
    file << body;
}

/**
 * @brief Length of a NUL-terminated literal.
 *
 * @param text The literal.
 * @return Its length.
 */
lpl::core::u32 length(const char *text) { return static_cast<lpl::core::u32>(std::strlen(text)); }

} // namespace

int main()
{
    std::printf("test-harvest-ingest — reading a corpus, and refusing to invent one\n");

    // ── The shape predicate, on its own ───────────────────────────────────────
    std::printf("── identifier shape\n");
    check("a three-digit identifier has the shape", lpl::harvest::looksLikeIdentifier("SIM-016", 7u));
    check("a digit in the prefix is allowed", lpl::harvest::looksLikeIdentifier("CT3-004", 7u));
    check("a two-letter prefix is allowed", lpl::harvest::looksLikeIdentifier("KN-007", 6u));
    // The measured false positive that made this rule what it is.
    check("FNV-1 does NOT have the shape", !lpl::harvest::looksLikeIdentifier("FNV-1", 5u));
    check("UTF-8 does not either", !lpl::harvest::looksLikeIdentifier("UTF-8", 5u));
    check("an all-digit prefix is refused", !lpl::harvest::looksLikeIdentifier("123-456", 7u));
    check("four digits are refused", !lpl::harvest::looksLikeIdentifier("SIM-0160", 8u));
    // @warning Shape alone cannot separate this from a real identifier, and the header says so.
    // What separates them is whether the corpus defines a member of the scheme.
    check("SHA-256 has the shape, and that is the limit of shape",
          lpl::harvest::looksLikeIdentifier("SHA-256", 7u));

    // ── Dates ─────────────────────────────────────────────────────────────────
    std::printf("── dates\n");
    lpl::core::i32 year = 0;
    const char *dated = "### Something — 2026-08-05";
    check("an ISO date is found", lpl::harvest::extractIsoYear(dated, length(dated), year) && year == 2026);
    const char *embedded = "build 20260805-1234 finished";
    check("a run inside a longer number is not a date",
          !lpl::harvest::extractIsoYear(embedded, length(embedded), year));
    const char *none = "## No date here";
    check("a heading with no date says so", !lpl::harvest::extractIsoYear(none, length(none), year));

    // ── A whole corpus ────────────────────────────────────────────────────────
    std::printf("── a corpus\n");
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lpl-harvest-fixture";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    // An index that DEFINES, in the table shape every index in this project uses.
    writeDocument(root / "index.md", "# Index\n"
                                     "\n"
                                     "## Items — 2026-08-05\n"
                                     "\n"
                                     "| id | what |\n"
                                     "|---|---|\n"
                                     "| `AAA-001` | the first thing |\n"
                                     "| `AAA-002` | the second thing |\n");

    // A document that CITES, plus a token of identifier shape whose scheme nothing defines,
    // plus a fenced block full of things that must not be read.
    writeDocument(root / "notes.md", "# Notes\n"
                                     "\n"
                                     "AAA-001 is the one that matters, and AAA-009 is not defined.\n"
                                     "We also hash with SHA-256, which is not an identifier here.\n"
                                     "\n"
                                     "```\n"
                                     "BBB-001 BBB-002 inside a fence\n"
                                     "```\n"
                                     "\n"
                                     "Back outside, AAA-002 again.\n");

    std::vector<lpl::harvest::MarkdownSource> sources{
        {(root / "index.md").string(), "fixture/index.md"},
        {(root / "notes.md").string(), "fixture/notes.md"},
    };

    lpl::harvest::Baker baker;
    lpl::harvest::IngestReport report{};
    check("the corpus is read", lpl::harvest::ingestMarkdown(sources, baker, report));
    check("both documents were read", report.documents == 2u);
    check("two identifiers are defined", report.definitions == 2u);
    check("nothing is defined twice", report.duplicates == 0u);

    // The scheme rule, which is the whole reason this reading is usable. AAA- is a real
    // scheme because index.md defines members of it, so AAA-009 is a genuine broken
    // reference. SHA- is not, so SHA-256 is noise and is counted apart.
    check("a real scheme with a missing member is a dangling reference",
          report.dangling == 1u && report.firstDangling == "AAA-009");
    check("a token whose scheme nothing defines is not a broken reference, it is noise",
          report.unknownScheme == 1u);

    // A fence is skipped whole. Without that, every enum value and hex constant in the
    // project's code samples would arrive as a citation. Four tokens of identifier shape sit
    // outside it — AAA-001, AAA-009, SHA-256, AAA-002 — and the two inside must not appear,
    // so six would mean the fence was read.
    check("nothing inside a fence was read", report.citations == 4u);

    check("the dated heading was seen", report.dated == 1u);

    // ── And it bakes into something readable ──────────────────────────────────
    std::vector<lpl::core::u8> image;
    lpl::harvest::BakeReport bake{};
    check("the corpus bakes", baker.build(image, bake));
    check("every claim has a described source", bake.unsourced == 0u);

    lpl::knowledge::KnowledgePack pack;
    check("the image opens", pack.open(image.data(), static_cast<lpl::core::u32>(image.size())) ==
                                 lpl::knowledge::OpenStatus::Ok);
    check("the predicates are named",
          pack.textFor(lpl::harvest::kPredicateDefinedIn) != nullptr &&
              std::strcmp(pack.textFor(lpl::harvest::kPredicateDefinedIn), "defined-in") == 0);

    // The question that made this worth writing: where is AAA-001 defined, and who cites it?
    const lpl::knowledge::FactStore store{pack};
    lpl::knowledge::Page page;
    lpl::knowledge::Query definitionOf;
    definitionOf.asserting(lpl::harvest::kPredicateDefinedIn);
    store.run(definitionOf, page);
    check("both definitions are retrievable", page.matched == 2u);

    lpl::knowledge::Query citations;
    citations.asserting(lpl::harvest::kPredicateCitedIn);
    store.run(citations, page);
    check("only resolvable citations were written", page.matched == 2u);

    // ── Footnotes ─────────────────────────────────────────────────────────────
    //
    // @warning The measurement that made this worth reading at all: this project's own book has 95
    // footnote definitions carrying only 20 DISTINCT numbers, because the numbering restarts
    // at every chapter. Keyed on the document, those 95 notes collapse onto 20 identifiers —
    // 75 reported as defined twice, and every reference resolving to whichever won.
    std::printf("── footnotes\n");
    {
        const std::filesystem::path notes = root / "notes";
        std::filesystem::create_directories(notes, error);

        // Two chapters, each with its own note 1 and note 2, each cited from its own prose.
        // This is the book's shape in miniature.
        writeDocument(notes / "book.md", "# Chapter one\n"
                                         "\n"
                                         "Ring 0 is privileged[^1] and the heap is bounded[^2].\n"
                                         "\n"
                                         "[^1]: The x86 privilege levels.\n"
                                         "[^2]: A bounded heap refuses rather than grows.\n"
                                         "\n"
                                         "# Chapter two\n"
                                         "\n"
                                         "Determinism is a whole-pipeline property[^1].\n"
                                         "\n"
                                         "[^1]: Fixed-point state replays bit-exactly.\n");

        std::vector<lpl::harvest::MarkdownSource> only{
            {(notes / "book.md").string(), "fixture/book.md"},
        };
        lpl::harvest::Baker b;
        lpl::harvest::IngestReport r{};
        check("a book with footnotes is read", lpl::harvest::ingestMarkdown(only, b, r));

        // Three notes, not two: chapter one's [^1] and chapter two's [^1] are different notes.
        check("a footnote number is scoped to its chapter", r.footnotes == 3u);
        check("nothing is reported as defined twice", r.duplicates == 0u);
        check("every reference was seen", r.footnoteCitations == 3u);
        check("and every one of them resolves", r.danglingFootnotes == 0u);

        // The name says which chapter, or two notes would read identically to a human too.
        check("the name carries the chapter",
              lpl::harvest::footnoteName("fixture/book.md", 2u, "1") == "fixture/book.md#ch2[^1]");
        check("and two chapters give two names",
              lpl::harvest::footnoteName("fixture/book.md", 1u, "1") !=
                  lpl::harvest::footnoteName("fixture/book.md", 2u, "1"));

        std::vector<lpl::core::u8> bytes;
        lpl::harvest::BakeReport br{};
        check("the book bakes", b.build(bytes, br));
        check("every claim has a described source", br.unsourced == 0u);

        lpl::knowledge::KnowledgePack pk;
        check("the image opens",
              pk.open(bytes.data(), static_cast<lpl::core::u32>(bytes.size())) == lpl::knowledge::OpenStatus::Ok);

        const lpl::knowledge::FactStore ks{pk};
        lpl::knowledge::Page pg;

        // The note's words travel with it. For a book that is the whole point: a reader
        // holding the image holds what note 1 of chapter 2 says, without opening the book.
        const lpl::core::u32 second = lpl::corpus::nameIdentifier(
            lpl::harvest::footnoteName("fixture/book.md", 2u, "1").data(),
            length(lpl::harvest::footnoteName("fixture/book.md", 2u, "1").c_str()));
        lpl::knowledge::Query words;
        words.about(second).asserting(lpl::harvest::kPredicateDefinitionText);
        ks.run(words, pg);
        check("a note carries its own words", pg.matched == 1u);
        const lpl::core::u8 *section = nullptr;
        lpl::core::u32 sectionBytes = 0u;
        lpl::corpus::TextView view;
        const char *line = nullptr;
        lpl::core::u32 size = 0u;
        check("the image carries a text section",
              pk.section(lpl::knowledge::SectionType::Texts, section, sectionBytes) &&
                  view.open(section, sectionBytes));
        check("and they are the right chapter's words",
              pg.count == 1u && view.line(pg.rows[0].object, line, size) &&
                  std::string(line, size) == " Fixed-point state replays bit-exactly.");

        lpl::knowledge::Query cited;
        cited.about(second).asserting(lpl::harvest::kPredicateCitedIn);
        ks.run(cited, pg);
        check("and knows where it is referred to", pg.matched == 1u);
    }

    // @warning The control: a reference whose chapter defines no such note is REFUSED, not attached
    // to a note of the same number in another chapter — which is exactly what document-wide
    // keying would have done, silently.
    {
        const std::filesystem::path notes = root / "notes";
        writeDocument(notes / "broken.md", "# Chapter one\n"
                                           "\n"
                                           "[^1]: A note that exists.\n"
                                           "\n"
                                           "# Chapter two\n"
                                           "\n"
                                           "This leans on a note its chapter never defines[^1].\n");
        std::vector<lpl::harvest::MarkdownSource> only{
            {(notes / "broken.md").string(), "fixture/broken.md"},
        };
        lpl::harvest::Baker b;
        lpl::harvest::IngestReport r{};
        check("the broken book is read", lpl::harvest::ingestMarkdown(only, b, r));
        check("one note is defined", r.footnotes == 1u);
        check("and the out-of-chapter reference is refused", r.danglingFootnotes == 1u);
    }

    // @warning And the converse control, which is the `SHA-256` rule one level up. Shape alone
    // cannot tell a reference from prose QUOTING the notation, and reading the latter as the
    // former is what buries the handful that really are broken. Measured on this project:
    // CLAUDE.md writes "notes [^19] [^20]" while discussing the book, and those two were the
    // only "dangling" footnotes in the whole corpus until this rule existed.
    {
        const std::filesystem::path notes = root / "notes";
        writeDocument(notes / "prose.md", "# A document that defines no notes\n"
                                          "\n"
                                          "The chapter gained two notes [^19] [^20] on that pass.\n");
        std::vector<lpl::harvest::MarkdownSource> only{
            {(notes / "prose.md").string(), "fixture/prose.md"},
        };
        lpl::harvest::Baker b;
        lpl::harvest::IngestReport r{};
        check("a document that only mentions the notation is read",
              lpl::harvest::ingestMarkdown(only, b, r));
        check("it defines no notes", r.footnotes == 0u);
        check("its markers are prose, not references", r.footnoteNoise == 2u);
        check("so nothing is reported as broken", r.danglingFootnotes == 0u);
        check("and no reference is counted either", r.footnoteCitations == 0u);
    }

    std::filesystem::remove_all(root, error);

    std::printf("\n%s (%d failures, %d checks)\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
