/**
 * @file Synthesis.hpp
 * @brief Map then reduce, so no study loses its context.
 *
 * Summarise each work alone with its hypotheses, sample and limits; only then compare.
 * Flattening a corpus into one prompt destroys exactly the structure a critical reading
 * depends on.
 *
 * **This is the one part of the harvest that needs a reader that can read.** Everything else
 * here is structural: `Markdown.hpp` extracts identifiers because a table row states them,
 * `ResearchReport.hpp` extracts findings because a writer emitted them as a list. Prose
 * states nothing structurally, and guessing at it with patterns would produce a corpus whose
 * confidences mean nothing — which is why neither of those files tries.
 *
 * @warning **So the model is INJECTED, and that is architecture rather than taste.** LplKnowledge
 * depends on LplPlugin and on nothing else; LplAssistant, which owns `infer/` and gate P14,
 * depends on both. Reaching from here into the inference side would invert that and make the
 * library unbuildable without a transformer. @ref IClaimReader is the seam, in the same
 * shape and for the same reason as `agent::IDecider`: the half that decides is separable
 * from the half that checks, so the checking half can be exercised with no model at all.
 *
 * @warning **And a proposal is not a claim until it is GROUNDED.** A reader may return anything;
 * what is admitted is only what @ref groundClaim can find in the passage it was given. That
 * is not a refinement, it is the whole safety argument — a model that invents an entity
 * produces a fact with perfect provenance pointing at a document that never mentioned it,
 * and no downstream reader can tell. Grounding is deterministic, needs nothing but the two
 * strings, and refuses by default.
 *
 * @warning What grounding does NOT do, stated because the distinction is easy to lose: it checks
 * that the WORDS are the source's, never that the claim is true. A passage saying 'Herodotus
 * wrongly reports that the walls were three hundred feet high' grounds a claim about
 * three-hundred-foot walls perfectly well. Truth is what confidence, corroboration and
 * `history::buildTimeline` are for; this only stops fabrication.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_SYNTHESIS_HPP
#    define LPL_LPL_HARVEST_SYNTHESIS_HPP

#    include <lpl/Foundation.hpp>
#    include <lpl/harvest/EntityResolution.hpp>

#    include <string>
#    include <string_view>
#    include <vector>

namespace lpl::harvest {

/**
 * @struct Passage
 * @brief One stretch of text to be read, and where it came from.
 */
struct Passage {
    core::u32 work{0u};   ///< Which work, by the caller's handle. Becomes the claim's source.
    core::u32 line{0u};   ///< First line of the passage in that work.
    core::u32 heading{0u}; ///< Which section it falls under.
    std::string text;     ///< The words themselves.
};

/**
 * @struct ProposedClaim
 * @brief What a reader thinks a passage asserts.
 *
 * Strings rather than identifiers, deliberately. A reader proposes words; turning words into
 * an identity is @ref EntityResolution's job and it is the job this file must not quietly do
 * for itself — a reader that minted its own identifiers would be resolving entities with no
 * view of the corpus, which is the one condition under which resolving them is unsafe.
 */
struct ProposedClaim {
    std::string subject;   ///< Who or what it is about.
    std::string predicate; ///< What is asserted of it.
    std::string object;    ///< The value asserted.
    core::i32 fromDay{0}; ///< First year covered.
    core::i32 toDay{0};   ///< Last year covered.
    core::u32 confidenceRaw{0u}; ///< How sure the reader is, raw Q16.16.
    /**
     * The words of the passage the reader says it read this out of.
     *
     * The load-bearing field. Without a quotation there is nothing to check a proposal
     * against, and a reader that returns claims with no quotation can only be trusted or
     * discarded wholesale — which is exactly the choice this design exists to avoid.
     */
    std::string quotation;
};

/**
 * @class IClaimReader
 * @brief The seam where something that can read prose is plugged in.
 *
 * Implemented on the inference side, and by a deterministic stand-in in tests. The interface
 * is deliberately narrow: one passage in, proposals out, no state carried between calls. A
 * reader that accumulated context across passages would make the corpus depend on the order
 * the harvester happened to walk it, and two machines would then disagree about history.
 */
class IClaimReader {
public:
    virtual ~IClaimReader() = default;

    /**
     * @brief Proposes what a passage asserts.
     *
     * @param passage The words, and where they came from.
     * @param out     Receives the proposals; the caller clears it first.
     */
    virtual void read(const Passage &passage, std::vector<ProposedClaim> &out) = 0;
};

/**
 * @enum Grounding
 * @brief Why a proposal was admitted, or what was wrong with it.
 */
enum class Grounding : core::u32 {
    Admitted = 0u,       ///< Every part of it is in the passage.
    NoQuotation = 1u,    ///< It cited nothing, so there is nothing to check.
    QuotationAbsent = 2u, ///< Its quotation is not in the passage. Fabricated.
    SubjectAbsent = 3u,  ///< Its quotation is real; the subject is not in it.
    ObjectAbsent = 4u,   ///< Likewise the object.
    Incomplete = 5u,     ///< A missing subject, predicate or object.
    ImpossibleWindow = 6u, ///< The window ends before it starts.
    OverConfident = 7u,  ///< A confidence outside [0, 1].
};

/**
 * @brief A stable word for a grounding outcome.
 *
 * @param grounding The outcome.
 * @return A short string; "unknown" for anything outside the enumeration.
 */
[[nodiscard]] const char *groundingText(Grounding grounding) noexcept;

/**
 * @brief Decides whether a passage really says what a reader claims it says.
 *
 * Four questions, in the order that makes the answer cheapest and the failure clearest:
 * is the proposal complete and self-consistent, is its quotation actually in the passage,
 * and are the subject and object actually in that quotation.
 *
 * The last two are what stop the most dangerous failure mode. A model given a passage about
 * Cambyses will happily quote it correctly and attribute the sentence to Cyrus, and the
 * result is a perfectly-cited fact about the wrong man — indistinguishable, downstream, from
 * a true one. Requiring both ends of the claim to appear inside the quoted span costs
 * nothing and makes that particular lie unwriteable.
 *
 * Comparison is case-insensitive over ASCII and collapses runs of whitespace, because a
 * reader that re-wraps a line has not fabricated anything. It is otherwise literal: no
 * stemming, no synonyms, nothing that could be talked into a match.
 *
 * @param passage  What was actually given to the reader.
 * @param proposal What came back.
 * @return @ref Grounding::Admitted, or the first thing found wrong.
 */
[[nodiscard]] Grounding groundClaim(const Passage &passage, const ProposedClaim &proposal) noexcept;

/**
 * @struct SynthesisReport
 * @brief What reading a corpus of prose produced, and what was thrown away.
 */
struct SynthesisReport {
    core::u32 passages{0u};  ///< Passages read.
    core::u32 proposed{0u};  ///< Claims a reader offered.
    core::u32 admitted{0u};  ///< Claims the passage actually supports.
    core::u32 refused{0u};   ///< Proposals rejected; see @ref refusals for the breakdown.
    core::u32 refusals[8]{}; ///< Count per @ref Grounding value.
    core::u32 works{0u};     ///< Distinct works the admitted claims came from.
};

/**
 * @struct SynthesisedClaim
 * @brief One admitted claim, with the work and line that back it.
 */
struct SynthesisedClaim {
    ProposedClaim claim;   ///< As proposed, unchanged — a claim rewritten is a claim invented.
    core::u32 work{0u};    ///< Which work said it.
    core::u32 line{0u};    ///< Where.
    core::u32 heading{0u}; ///< Under which section.
};

/**
 * @brief Reads a corpus of prose into claims, one work at a time.
 *
 * The MAP half of map-then-reduce. Each passage is read alone, so a work's claims arrive
 * with that work's context rather than with the average of the corpus, and each is grounded
 * against the passage it came from before it is kept.
 *
 * The REDUCE half is deliberately NOT here: comparing works is
 * @ref countIndependentAgreements, which already knows what makes two sources one witness.
 * Writing a second comparison next to it would give the corpus two answers to how much it
 * agrees with itself.
 *
 * @param passages What to read.
 * @param reader   Who reads it.
 * @param out      Receives the admitted claims; cleared first.
 * @param report   Receives the tally.
 * @return false when a reader misbehaves in a way that makes the batch untrustworthy rather
 *         than merely wrong — currently, proposing more claims for one passage than
 *         @ref kMaxProposalsPerPassage allows.
 */
[[nodiscard]] bool synthesise(const std::vector<Passage> &passages, IClaimReader &reader,
                              std::vector<SynthesisedClaim> &out, SynthesisReport &report);

/**
 * Most claims one passage may yield.
 *
 * A bound rather than a trust: the reader is a model, its output is not under this
 * repository's control, and a runaway generation must cost a bounded amount of memory. It is
 * a REFUSAL of the batch rather than a silent truncation — quietly keeping the first fifty
 * of a thousand would read, downstream, as a passage that only said fifty things.
 */
inline constexpr core::u32 kMaxProposalsPerPassage = 64u;

/**
 * @brief Turns admitted claims into testimonies for the corroboration count.
 *
 * The join between this file and @ref countIndependentAgreements, written once here so that
 * "the same claim" means one thing in the whole repository. Two claims are the same when
 * their subject, predicate and object match after the same normalisation grounding uses —
 * an EXACT match, deliberately: grouping near-duplicates would find more agreement by
 * deciding that two differently-worded sentences say the same thing, and that judgement
 * inflates confidence exactly when it is wrong. Understating agreement is the safe error.
 *
 * @param claims The admitted claims.
 * @param out    Receives one testimony per claim; cleared first.
 */
void testimoniesFor(const std::vector<SynthesisedClaim> &claims, std::vector<Testimony> &out);

} // namespace lpl::harvest

#endif // LPL_LPL_HARVEST_SYNTHESIS_HPP
