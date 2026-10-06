/**
 * @file test_harvest_research.cpp
 * @brief Reading a research run into the library, and refusing what has no provenance.
 *
 * @warning **The measurement this file exists because of.** A real run — eleven kilobytes, seven
 * cited sources, twenty extracted findings — was dropped into a harvested root and ingested.
 * It baked to **zero facts**. Nothing was broken: the librarian read markdown looking for
 * this project's `SIM-016` identifier scheme, a research report has none, and so the image
 * recorded that a document exists and knew nothing about what it said.
 *
 * The fixtures are written by the test rather than checked in, for the reason
 * `test_harvest_ingest.cpp` already gives: what is under test is a READER, so the corpus has
 * to be controlled down to the line number.
 *
 * @warning **And that is also this file's limit, stated rather than papered over.** A fixture typed
 * into the reader's own test proves only that the reader agrees with itself; it cannot catch
 * the writer drifting. The link to the real writer is made elsewhere and on purpose:
 * `test-research-report` in LplAssistant compiles the writer's own assembler and emits a
 * report, and the full validation feeds THAT file to `lpl-ingest`. Neither check is sufficient
 * alone, which is why there are two.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include <lpl/harvest/Baker.hpp>
#include <lpl/harvest/Markdown.hpp>
#include <lpl/harvest/ResearchReport.hpp>
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
void writeDocument(const std::filesystem::path &path, const std::string &body)
{
    std::ofstream file{path, std::ios::binary};
    file << body;
}

/// A report in `writeReport`'s shape, with everything a reader has to tell apart.
const char *const kWholeReport =
    "# Deterministic replay across targets\n"
    "\n"
    "*Rapport Laplace deep research — 2026-08-06 09:30 — 6 pas, ~12345 tokens*\n"
    "\n"
    "## Synthèse\n"
    "\n"
    "Fixed-point state is what makes a replay bit-exact [S1].\n"
    "\n"
    "## Constats\n"
    "\n"
    "- [S1] Fixed-point arithmetic replays bit-exactly across targets.\n"
    "- [S2] Determinism is a property of the whole pipeline.\n"
    "\n"
    "## Sources\n"
    "\n"
    "- [S1] A deterministic replay study — <https://example.org/paper> (openalex) \xE2\x9C\x93\n"
    "- [S2] Determinism — <https://example.org/wiki> (wikipedia) \xE2\x9C\x97\n"
    "- [S3] ~~https://example.org/blocked~~ (lecture échouée)\n"
    "\n"
    "## Limites\n"
    "\n"
    "- Texte intégral des pages en cache : `cache/` (ids [S] ci-dessus).\n";

} // namespace

int main()
{
    std::printf("test-harvest-research — a run becomes a cartridge, or it is refused\n");

    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lplknow-research-fixture";
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    // ── Telling a report from anything else ───────────────────────────────────
    std::printf("── recognition\n");
    check("a report is recognised", lpl::harvest::looksLikeResearchReport(kWholeReport));
    check("plain prose is not a report",
          !lpl::harvest::looksLikeResearchReport("# Notes\n\nSIM-016 is defined elsewhere.\n"));
    // The discriminator is the writer's byline, so a document that merely has headings with
    // the same names is not a report. Without this, any French document with a `## Sources`
    // section would be read by the wrong reader and would silently contribute nothing.
    check("headings alone do not make a report",
          !lpl::harvest::looksLikeResearchReport("# X\n\n## Constats\n\n- [S1] a\n\n## Sources\n\n- [S1] b\n"));

    // ── Provider weights ──────────────────────────────────────────────────────
    std::printf("── what a source is worth\n");
    const lpl::harvest::ProviderProfile peer = lpl::harvest::providerProfile("openalex");
    const lpl::harvest::ProviderProfile wiki = lpl::harvest::providerProfile("wikipedia");
    const lpl::harvest::ProviderProfile odd = lpl::harvest::providerProfile("something-new");
    check("a peer-reviewed source outweighs an encyclopaedia", peer.confidenceRaw > wiki.confidenceRaw);
    check("an unknown provider is worth least of all", odd.confidenceRaw < wiki.confidenceRaw);
    check("every confidence is a proper probability",
          peer.confidenceRaw <= 65536u && wiki.confidenceRaw <= 65536u && odd.confidenceRaw <= 65536u);
    check("a peer-reviewed paper maps onto the checkable posture",
          peer.kind == lpl::knowledge::SourceKindV1::Notarial);

    // ── The whole report ──────────────────────────────────────────────────────
    std::printf("── a complete run\n");
    writeDocument(root / "report.md", kWholeReport);
    std::vector<lpl::harvest::ResearchDocument> documents{
        {(root / "report.md").string(), "fixture/report.md"},
    };

    lpl::harvest::Baker baker;
    lpl::harvest::ResearchIngestReport report{};
    check("the run is read", lpl::harvest::ingestResearchReports(documents, baker, report));
    check("one report was read", report.reports == 1u);
    check("both findings were read", report.findings == 2u);
    check("no finding is orphaned", report.orphanFindings == 0u);
    check("three sources were read", report.sources == 3u);
    check("the unfetchable source is counted apart", report.unreadable == 1u);
    // One tick, one cross, one struck-through. Only the tick is evidence of anything
    // durable, so exactly one source may carry a reachability claim.
    check("only a confirmed source counts as reachable", report.reachable == 1u);
    check("the byline supplied a date", report.dated == 1u);

    std::vector<lpl::core::u8> image;
    lpl::harvest::BakeReport bake{};
    check("the run bakes", baker.build(image, bake));
    check("every claim has a described source", bake.unsourced == 0u);
    check("nothing collided", bake.collisions == 0u);

    lpl::knowledge::KnowledgePack pack;
    check("the image opens", pack.open(image.data(), static_cast<lpl::core::u32>(image.size())) ==
                                 lpl::knowledge::OpenStatus::Ok);
    check("the predicates are named",
          pack.textFor(lpl::harvest::kPredicateRestsOn) != nullptr &&
              std::strcmp(pack.textFor(lpl::harvest::kPredicateRestsOn), "rests-on") == 0);
    // `cited-in` is shared with the markdown reader, and a batch of reports with no markdown
    // alongside must still resolve it. Measured, not feared: it printed `#1002` once.
    check("a reused predicate is still named here",
          pack.textFor(lpl::harvest::kPredicateCitedIn) != nullptr &&
              std::strcmp(pack.textFor(lpl::harvest::kPredicateCitedIn), "cited-in") == 0);

    const lpl::knowledge::FactStore store{pack};
    lpl::knowledge::Page page;

    lpl::knowledge::Query rests;
    rests.asserting(lpl::harvest::kPredicateRestsOn);
    store.run(rests, page);
    check("every finding is retrievable with the source that backs it", page.matched == 2u);

    lpl::knowledge::Query alive;
    alive.asserting(lpl::harvest::kPredicateReachable);
    store.run(alive, page);
    check("only the confirmed URL carries a reachability claim", page.matched == 1u);

    lpl::knowledge::Query topic;
    topic.asserting(lpl::harvest::kPredicateResearches);
    store.run(topic, page);
    check("what the run set out to answer is retrievable", page.matched == 1u);

    // A finding's confidence is the source's, not the report's. Two findings from two
    // providers must not arrive equally believed — if they did, the whole provider table
    // would be decoration.
    lpl::knowledge::Query weighed;
    weighed.asserting(lpl::harvest::kPredicateRestsOn);
    store.run(weighed, page);
    bool differ = false;
    if (page.count >= 2u)
        differ = page.rows[0].confidenceRaw != page.rows[1].confidenceRaw;
    check("findings are believed according to their source", differ);

    // ── The controls: what must NOT happen ────────────────────────────────────
    std::printf("── refusals\n");

    // (1) A report with no findings section yields no findings. This is the check that
    //     catches the WRITER regressing: if `renderFindings` ever stops being emitted, this
    //     is what a real corpus starts looking like, and a reader that quietly invented
    //     findings from the prose would hide it.
    {
        std::string stripped{kWholeReport};
        const std::size_t at = stripped.find("## Constats");
        const std::size_t to = stripped.find("## Sources");
        stripped.erase(at, to - at);
        writeDocument(root / "nofindings.md", stripped);
        lpl::harvest::Baker b;
        lpl::harvest::ResearchIngestReport r{};
        std::vector<lpl::harvest::ResearchDocument> d{{(root / "nofindings.md").string(), "fixture/nf.md"}};
        check("a report with no findings section is still read", lpl::harvest::ingestResearchReports(d, b, r));
        check("and yields no findings at all", r.findings == 0u);
        check("while still recording what it consulted", r.sources == 3u);
    }

    // (2) A finding citing a source the report does not list is REFUSED, not baked. Writing
    //     it would make the image assert that something was learned from a source nothing
    //     can name — a claim with no provenance, which is the one property this format
    //     exists to keep.
    {
        std::string orphaned{kWholeReport};
        const std::size_t at = orphaned.find("- [S2] Determinism");
        orphaned.replace(at, std::strlen("- [S2]"), "- [S9]");
        writeDocument(root / "orphan.md", orphaned);
        lpl::harvest::Baker b;
        lpl::harvest::ResearchIngestReport r{};
        std::vector<lpl::harvest::ResearchDocument> d{{(root / "orphan.md").string(), "fixture/or.md"}};
        check("a report with an orphaned finding is still read", lpl::harvest::ingestResearchReports(d, b, r));
        check("the orphan is counted", r.orphanFindings == 1u && r.firstOrphan == "S9");
        check("and only the sourced finding survives", r.findings == 1u);

        std::vector<lpl::core::u8> bytes;
        lpl::harvest::BakeReport br{};
        check("the image still bakes", b.build(bytes, br));
        check("with no unsourced claim in it", br.unsourced == 0u);
    }

    // (3) One URL is one source, across every report in the batch. Getting this wrong is not
    //     a lost lookup: `history::fuseConfidence` would treat one paper cited twice as two
    //     concurring witnesses and manufacture certainty nobody earned.
    {
        writeDocument(root / "second.md", kWholeReport);
        lpl::harvest::Baker b;
        lpl::harvest::ResearchIngestReport r{};
        std::vector<lpl::harvest::ResearchDocument> d{
            {(root / "report.md").string(), "fixture/one.md"},
            {(root / "second.md").string(), "fixture/two.md"},
        };
        check("two reports are read", lpl::harvest::ingestResearchReports(d, b, r) && r.reports == 2u);
        check("both reports' findings are kept", r.findings == 4u);
        check("but the same URL is ONE source, not two", r.sources == 3u);
    }

    std::filesystem::remove_all(root, error);

    std::printf("\n%s (%d failures, %d checks)\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
