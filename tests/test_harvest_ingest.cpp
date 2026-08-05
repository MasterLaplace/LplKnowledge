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
    // ⚠ Shape alone cannot separate this from a real identifier, and the header says so.
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

    std::filesystem::remove_all(root, error);

    std::printf("\n%s (%d failures, %d checks)\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
