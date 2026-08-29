/**
 * @file test_knowledge_parity.cpp
 * @brief Gate P18 `corpus` — the host oracle, and the round trip that must lose nothing.
 *
 * What is under test is a TRANSLATION, not a computation, so the check that matters is not
 * "does the image parse" but "is the history built from the DECODED corpus the same
 * history, bit for bit, as the one gate P13 builds from the corpus in memory". A format can
 * round-trip into something that opens cleanly and has quietly rounded one confidence, and
 * the consequence of that is a different consensus about how a king died.
 *
 * The corpus is declared ONCE, in `history::parityCorpus`. Nothing here restates it: a test
 * that spelled the corpus out on both sides would be testing transcription.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include <lpl/harvest/Baker.hpp>
#include <lpl/history/Parity.hpp>
#include <lpl/knowledge/FactStore.hpp>
#include <lpl/knowledge/History.hpp>
#include <lpl/knowledge/Parity.hpp>
#include <lpl/knowledge/Provenance.hpp>

#include <cstdio>
#include <cstring>
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

} // namespace

int main()
{
    std::printf("test-knowledge-parity — gate P18 corpus (host oracle)\n");

    // ── The bake ──────────────────────────────────────────────────────────────
    std::vector<lpl::core::u8> image;
    lpl::harvest::BakeReport bake{};
    check("the canonical corpus bakes", lpl::harvest::bakeParityCorpus(image, bake));
    check("it wrote every claim the corpus holds", bake.facts == 5u);
    check("and every source", bake.sources == 4u);
    check("one document and one locus", bake.documents == 1u && bake.loci == 1u);
    check("no collision", bake.collisions == 0u);
    check("no unsourced claim", bake.unsourced == 0u);
    check("the image is not empty", bake.bytes > 0u && bake.bytes == image.size());
    std::printf("   baked %u bytes, %u sections, %u names\n", bake.bytes, bake.sections, bake.vocabulary);

    // Baking twice must give the same bytes. That is what "the order is canonical" means, and
    // without it every downstream signature would be a signature of whichever order the sort
    // happened to leave — which is not stable across standard library versions.
    std::vector<lpl::core::u8> again;
    lpl::harvest::BakeReport twice{};
    (void) lpl::harvest::bakeParityCorpus(again, twice);
    check("baking twice gives the same bytes",
          again.size() == image.size() && std::memcmp(again.data(), image.data(), image.size()) == 0);

    // ── The reader ────────────────────────────────────────────────────────────
    lpl::knowledge::KnowledgePack pack;
    const lpl::knowledge::OpenStatus status = pack.open(image.data(), static_cast<lpl::core::u32>(image.size()));
    check("the baked image opens", status == lpl::knowledge::OpenStatus::Ok);
    if (status != lpl::knowledge::OpenStatus::Ok)
    {
        std::printf("\nFAILURES (%d failures, %d checks) — %s\n", gFailures, gChecks,
                    lpl::knowledge::openStatusText(status));
        return 1;
    }
    check("nothing was skipped", pack.skippedSections() == 0u);
    check("the vocabulary resolves an identifier",
          pack.textFor(lpl::history::kSubjectKing) != nullptr &&
              std::strcmp(pack.textFor(lpl::history::kSubjectKing), "king") == 0);
    check("an unnamed identifier is absent, not a failure", pack.textFor(999999u) == nullptr);

    // One flipped byte must be refused. The content hash covers everything after the header,
    // so this is also what says the hash is being CHECKED rather than merely written.
    for (const lpl::core::u32 at : {40u, 64u, static_cast<lpl::core::u32>(image.size() - 1u)})
    {
        std::vector<lpl::core::u8> damaged = image;
        damaged[at] = static_cast<lpl::core::u8>(damaged[at] ^ 0xFFu);
        lpl::knowledge::KnowledgePack broken;
        check("one flipped byte is refused",
              broken.open(damaged.data(), static_cast<lpl::core::u32>(damaged.size())) !=
                  lpl::knowledge::OpenStatus::Ok);
    }

    // ── Provenance ────────────────────────────────────────────────────────────
    lpl::knowledge::ProvenanceAudit audit{};
    check("every claim can be weighed", lpl::knowledge::auditProvenance(pack, audit));
    check("the audit saw every claim", audit.facts == 5u);
    check("no claim is unsourced", audit.unsourced == 0u);
    check("nothing dangles", audit.danglingLocus == 0u && audit.danglingDocument == 0u);
    // Four of the five claims carry no locus, and that is LEGITIMATE — plenty of knowledge is
    // held without a line number. Counted apart from a dangling one on purpose.
    check("claims without a locus are counted, not faulted", audit.unlocated == 4u);

    lpl::knowledge::FactV1 cited{};
    bool foundCited = false;
    for (lpl::core::u32 i = 0u; i < pack.factCount(); ++i)
    {
        lpl::knowledge::FactV1 fact{};
        if (pack.factAt(i, fact) && fact.locus != lpl::knowledge::kNoIdentifier)
        {
            cited = fact;
            foundCited = true;
            break;
        }
    }
    check("the cited claim is there", foundCited);
    if (foundCited)
    {
        lpl::knowledge::Citation citation{};
        check("it resolves to a citation", lpl::knowledge::cite(pack, cited, citation));
        check("with a source, a document and a locus",
              citation.hasDocument == 1u && citation.hasLocus == 1u && citation.sourceName != nullptr);
        char rendered[192];
        (void) lpl::knowledge::renderCitation(citation, rendered, 192u);
        check("and it renders the passage the baker recorded", std::strstr(rendered, "3.12.412") != nullptr);
        check("naming the source", std::strstr(rendered, "chronicler") != nullptr);
        std::printf("   citation: %s\n", rendered);
    }

    // ── The round trip ────────────────────────────────────────────────────────
    lpl::history::Corpus decoded;
    lpl::knowledge::DecodeReport report{};
    check("the image decodes with nothing rejected", lpl::knowledge::toHistoryCorpus(pack, decoded, report));
    check("no record was refused", report.badKind == 0u && report.badWindow == 0u && report.badConfidence == 0u);

    lpl::history::Corpus authored;
    lpl::history::parityCorpus(authored);
    // Field for field, and on the RAW sigma word: a comparison with a tolerance is a
    // comparison that cannot see the bit a round trip lost.
    check("the decoded corpus is the authored corpus", lpl::knowledge::corporaMatch(decoded, authored));

    // The claim Baker.hpp makes about the canonical corpus: its authored order already IS the
    // canonical order, which is why the round trip is exact as a SEQUENCE and not merely as a
    // set. Asserted rather than assumed, because it is a property of that corpus and not of
    // the format.
    bool sameOrder = decoded.facts.size() == authored.facts.size();
    for (std::size_t i = 0u; sameOrder && i < decoded.facts.size(); ++i)
        sameOrder = decoded.facts[i].source == authored.facts[i].source &&
                    decoded.facts[i].object == authored.facts[i].object;
    check("the canonical corpus is authored in canonical order", sameOrder);

    // ── The refusals a writer owes ────────────────────────────────────────────
    {
        lpl::harvest::Baker collider;
        check("naming an identifier twice with the same word is idempotent",
              collider.name(42u, "king") && collider.name(42u, "king"));
        check("naming it with a DIFFERENT word is refused", !collider.name(42u, "queen"));
        std::vector<lpl::core::u8> discarded;
        lpl::harvest::BakeReport refused{};
        check("and the bake is refused", !collider.build(discarded, refused));
        check("with the collision reported", refused.collisions == 1u && !refused.firstCollision.empty());
    }
    {
        lpl::harvest::Baker orphan;
        lpl::knowledge::FactV1 fact{};
        fact.subject = 1u;
        fact.predicate = 2u;
        fact.object = 3u;
        fact.source = 777u; // described by nothing
        fact.confidenceRaw = 32768u;
        orphan.addFact(fact);
        std::vector<lpl::core::u8> discarded;
        lpl::harvest::BakeReport refused{};
        check("a claim whose source nothing describes is refused", !orphan.build(discarded, refused));
        check("and counted", refused.unsourced == 1u);
    }

    // ── The decoder's refusals ────────────────────────────────────────────────
    {
        lpl::knowledge::FactV1 wire{};
        wire.confidenceRaw = 65537u; // above one
        lpl::history::Fact fact;
        check("a confidence above one is refused, not clamped", !lpl::knowledge::fromWireFact(wire, fact));

        wire.confidenceRaw = 32768u;
        wire.fromDay = 1300;
        wire.toDay = 1200;
        check("a reversed window is refused", !lpl::knowledge::fromWireFact(wire, fact));

        lpl::knowledge::SourceV1 source{};
        source.kind = static_cast<lpl::core::u32>(lpl::knowledge::SourceKindV1::Count);
        lpl::history::SourceProfile profile;
        check("an unknown source kind is refused", !lpl::knowledge::fromWireSource(source, profile));
    }

    // ── The fold, and its equality with gate P13 ──────────────────────────────
    lpl::knowledge::KnowledgeFoldResult fold{};
    lpl::knowledge::foldKnowledgeState(image.data(), static_cast<lpl::core::u32>(image.size()), fold);

    check("the fold accepted the image", fold.openStatus == 0u);
    check("it round-tripped", fold.roundTrip == 1u);
    check("it rejected nothing", fold.decodeRejected == 0u);
    check("provenance is sound", fold.provenanceOk == 1u);
    check("the canonical query found both contradictory claims", fold.queryMatched == 2u);
    check("and did not have to truncate", fold.queryTruncated == 0u);
    check("the consensus is the osteologist's", fold.consensusObject == lpl::history::kObjectDysentery);

    lpl::history::HistoryFoldResult p13{};
    lpl::history::foldHistoryState(p13);
    // THE claim of this gate. Not that the numbers are stable — that they are the SAME numbers
    // gate P13 produces from a corpus that never touched a file.
    check("P18 timeline == P13 timeline", fold.timelineSignature == p13.timelineSignature);
    check("P18 chronicle == P13 chronicle", fold.chronicleSignature == p13.chronicleSignature);
    check("P18 minority == P13 minority", fold.minoritySignature == p13.minoritySignature);

    // A control: the three equalities above would hold just as well if every signature were
    // zero, which is the shape of a gate that passes without having run.
    check("the signatures are not trivially zero",
          fold.timelineSignature != 0u && fold.imageSignature != 0u && fold.factSignature != 0u &&
              fold.vocabularySignature != 0u && fold.pageSignature != 0u && fold.citationSignature != 0u);

    // The line the kernel prints, in the same shape, so the two can be compared field by field
    // without anyone re-deriving which name means what.
    std::printf("P18 corpus: image=0x%08X fact=0x%08X vocab=0x%08X audit=0x%08X page=0x%08X citation=0x%08X, "
                "timeline=0x%08X chronicle=0x%08X minority=0x%08X, bytes=%u sections=%u skipped=%u facts=%u "
                "sources=%u documents=%u loci=%u names=%u matched=%u returned=%u truncated=%u consensus=%u "
                "provenance=%u round_trip=%u rejected=%u\n",
                fold.imageSignature, fold.factSignature, fold.vocabularySignature, fold.auditSignature,
                fold.pageSignature, fold.citationSignature, fold.timelineSignature, fold.chronicleSignature,
                fold.minoritySignature, fold.imageBytes, fold.sections, fold.skipped, fold.facts, fold.sources,
                fold.documents, fold.loci, fold.vocabulary, fold.queryMatched, fold.queryReturned,
                fold.queryTruncated, fold.consensusObject, fold.provenanceOk, fold.roundTrip, fold.decodeRejected);

    // The project's verdict format, character for character: validate.sh greps for
    // "ALL PASS (0 failure", so a line that says the same thing in another order is a
    // test that passes and is recorded as a failure.
    std::printf("\n%s (%d failures, %d checks)\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
