/**
 * @file test_synthesis.cpp
 * @brief What a passage says, and everything a reader might say it says instead.
 *
 * The reader here is a fixture, not a model, and that is the design being tested rather than
 * a shortcut around it: `IClaimReader` exists so the half that CHECKS can be exercised
 * without the half that GUESSES. A grounding rule that could only be run behind an inference
 * pass would be a safety argument nobody could re-run.
 *
 * So the fixtures are adversarial on purpose. Each one is a specific lie a language model
 * actually tells — a quotation that is not in the text, a real quotation attributed to the
 * wrong subject, an object that appears nowhere — and each must be refused with the reason
 * that names it.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include <lpl/harvest/EntityResolution.hpp>
#include <lpl/harvest/Synthesis.hpp>

#include <cstdio>
#include <cstring>
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

/// A passage naming two people, which is what makes misattribution possible.
constexpr const char *kPassage =
    "Cyrus founded the empire and reigned twenty-nine years.\n"
    "Cambyses his son marched into Egypt and took Memphis.";

/**
 * @brief Builds a well-formed proposal.
 *
 * @return A claim the passage genuinely supports.
 */
[[nodiscard]] lpl::harvest::ProposedClaim sound()
{
    lpl::harvest::ProposedClaim claim;
    claim.subject = "Cambyses";
    claim.predicate = "took";
    claim.object = "Memphis";
    claim.fromDay = -525;
    claim.toDay = -525;
    claim.confidenceRaw = 45000u;
    claim.quotation = "Cambyses his son marched into Egypt and took Memphis.";
    return claim;
}

/**
 * @class ScriptedReader
 * @brief A reader that returns whatever the test told it to.
 */
class ScriptedReader final : public lpl::harvest::IClaimReader {
public:
    /**
     * @brief Sets what the next read returns.
     *
     * @param claims The script.
     */
    explicit ScriptedReader(std::vector<lpl::harvest::ProposedClaim> claims) : _claims(std::move(claims)) {}

    /**
     * @brief Returns the script, ignoring the passage.
     *
     * @param passage Unused.
     * @param out     Receives the script.
     */
    void read(const lpl::harvest::Passage &passage, std::vector<lpl::harvest::ProposedClaim> &out) override
    {
        (void) passage;
        out = _claims;
    }

private:
    std::vector<lpl::harvest::ProposedClaim> _claims;
};

/**
 * @brief Grounds one proposal against the standard passage.
 *
 * @param claim The proposal.
 * @return The outcome.
 */
[[nodiscard]] lpl::harvest::Grounding ground(const lpl::harvest::ProposedClaim &claim)
{
    lpl::harvest::Passage passage;
    passage.work = 1u;
    passage.line = 10u;
    passage.text = kPassage;
    return lpl::harvest::groundClaim(passage, claim);
}

} // namespace

int main()
{
    std::printf("test-synthesis — a proposal is not a claim until the passage says so\n");
    using lpl::harvest::Grounding;

    // ── What the passage does support ─────────────────────────────────────────
    std::printf("── admitted\n");
    check("a claim the passage states is admitted", ground(sound()) == Grounding::Admitted);

    {
        // Re-wrapping and re-casing are not fabrication, so they are forgiven — and nothing
        // beyond them is.
        lpl::harvest::ProposedClaim claim = sound();
        claim.quotation = "cambyses his son marched into egypt\n   and took memphis.";
        check("re-wrapping and re-casing a quotation is still the same quotation",
              ground(claim) == Grounding::Admitted);
    }

    // ── Every lie a reader actually tells ─────────────────────────────────────
    std::printf("── refused\n");

    {
        lpl::harvest::ProposedClaim claim = sound();
        claim.quotation = "Cambyses razed the temples of Memphis and was cursed for it.";
        check("a quotation the passage does not contain is refused",
              ground(claim) == Grounding::QuotationAbsent);
    }

    {
        // @warning The dangerous one. The quotation is real, word for word, and the subject is a
        // real person named in the same passage — just not the one the sentence is about.
        // The result would be a perfectly-cited fact about the wrong man.
        lpl::harvest::ProposedClaim claim = sound();
        claim.subject = "Cyrus";
        check("a real quotation attributed to the wrong subject is refused",
              ground(claim) == Grounding::SubjectAbsent);
    }

    {
        lpl::harvest::ProposedClaim claim = sound();
        claim.object = "Babylon";
        check("an object that is not in the quoted span is refused", ground(claim) == Grounding::ObjectAbsent);
    }

    {
        // Present in the passage but NOT in the quoted span. This is the check that makes
        // the span mean something: without it, any two facts in one passage could be
        // recombined into a third that the passage never states.
        lpl::harvest::ProposedClaim claim = sound();
        claim.object = "empire";
        check("a term from elsewhere in the passage is still not in this quotation",
              ground(claim) == Grounding::ObjectAbsent);
    }

    {
        lpl::harvest::ProposedClaim claim = sound();
        claim.quotation.clear();
        check("a claim citing nothing is refused", ground(claim) == Grounding::NoQuotation);
    }

    {
        lpl::harvest::ProposedClaim claim = sound();
        claim.predicate.clear();
        check("an incomplete claim is refused", ground(claim) == Grounding::Incomplete);
    }

    {
        lpl::harvest::ProposedClaim claim = sound();
        claim.fromDay = -500;
        claim.toDay = -525;
        check("a window that ends before it starts is refused", ground(claim) == Grounding::ImpossibleWindow);
    }

    {
        lpl::harvest::ProposedClaim claim = sound();
        claim.confidenceRaw = 70000u;
        check("a confidence above one is refused", ground(claim) == Grounding::OverConfident);
    }

    check("every outcome has a word", std::strcmp(lpl::harvest::groundingText(Grounding::SubjectAbsent),
                                                  "subject absent") == 0);

    // ── The map half ──────────────────────────────────────────────────────────
    std::printf("── reading a corpus\n");
    {
        lpl::harvest::ProposedClaim liar = sound();
        liar.quotation = "Cambyses conquered the whole of Libya without a battle.";
        ScriptedReader reader{{sound(), liar}};

        std::vector<lpl::harvest::Passage> passages;
        passages.push_back({1u, 10u, 2u, kPassage});
        passages.push_back({2u, 40u, 3u, kPassage});

        std::vector<lpl::harvest::SynthesisedClaim> claims;
        lpl::harvest::SynthesisReport report{};
        check("the corpus is read", lpl::harvest::synthesise(passages, reader, claims, report));
        check("both passages were read", report.passages == 2u);
        check("all four proposals were seen", report.proposed == 4u);
        check("only the grounded ones survive", report.admitted == 2u && claims.size() == 2u);
        check("and the rest are refused with a reason",
              report.refused == 2u &&
                  report.refusals[static_cast<lpl::core::u32>(Grounding::QuotationAbsent)] == 2u);
        check("a claim keeps the work that said it", claims[0].work == 1u && claims[1].work == 2u);
        check("and the line it was found on", claims[0].line == 10u && claims[1].line == 40u);
        check("an admitted claim is stored unchanged", claims[0].claim.object == "Memphis");
        check("two works contributed", report.works == 2u);
    }

    {
        // A runaway generation is REFUSED, never truncated. Quietly keeping the first sixty-
        // four would read downstream as a passage that only said sixty-four things.
        std::vector<lpl::harvest::ProposedClaim> flood(lpl::harvest::kMaxProposalsPerPassage + 1u, sound());
        ScriptedReader reader{flood};
        std::vector<lpl::harvest::Passage> passages{{1u, 1u, 1u, kPassage}};
        std::vector<lpl::harvest::SynthesisedClaim> claims;
        lpl::harvest::SynthesisReport report{};
        check("a runaway reader fails the batch rather than being trimmed",
              !lpl::harvest::synthesise(passages, reader, claims, report));
    }

    // ── The join to the reduce half ───────────────────────────────────────────
    std::printf("── corroboration\n");
    {
        // Two DIFFERENT works stating the same thing corroborate. This is the composition
        // the two files exist to make: synthesis produces testimonies, entity resolution
        // decides how many witnesses those testimonies really represent.
        std::vector<lpl::harvest::SynthesisedClaim> claims;
        claims.push_back({sound(), 1u, 10u, 1u});
        claims.push_back({sound(), 2u, 20u, 1u});

        std::vector<lpl::harvest::Testimony> testimonies;
        lpl::harvest::testimoniesFor(claims, testimonies);
        check("one testimony per claim", testimonies.size() == 2u);
        check("the same claim from two works is the SAME claim",
              testimonies[0].claim == testimonies[1].claim);
        check("asserted by different sources", testimonies[0].source != testimonies[1].source);

        lpl::harvest::AgreementReport agreement{};
        check("the corroboration is counted",
              lpl::harvest::countIndependentAgreements({1u, 2u}, {"urn:a", "urn:b"}, testimonies, {},
                                                       agreement));
        check("two independent works corroborate it", agreement.corroborated == 1u);

        // @warning And the control that makes that mean something: the same two testimonies, when
        // the two works turn out to be one document, must NOT corroborate.
        lpl::harvest::AgreementReport fused{};
        check("the same corpus is counted with one shared address",
              lpl::harvest::countIndependentAgreements({1u, 2u}, {"https://doi.org/10.1/x", "10.1/x"},
                                                       testimonies, {}, fused));
        check("one work under two names corroborates nothing", fused.corroborated == 0u);
        check("and says so", fused.inflated == 1u);
    }

    {
        // A different claim is a different claim: the identity must not collapse two
        // statements that share two of their three parts.
        lpl::harvest::ProposedClaim other = sound();
        other.object = "Egypt";
        other.quotation = "Cambyses his son marched into Egypt and took Memphis.";
        std::vector<lpl::harvest::SynthesisedClaim> claims;
        claims.push_back({sound(), 1u, 10u, 1u});
        claims.push_back({other, 2u, 20u, 1u});
        std::vector<lpl::harvest::Testimony> testimonies;
        lpl::harvest::testimoniesFor(claims, testimonies);
        check("two different claims are two claims", testimonies[0].claim != testimonies[1].claim);
    }

    std::printf("\n%s (%d failures, %d checks)\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
