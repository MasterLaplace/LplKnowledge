/**
 * @file EntityResolution.hpp
 * @brief Deciding that two names denote one thing.
 *
 * 'The Sun King' and 'Louis XIV' must collapse, and two homonymous notaries must not.
 * Errors here are invisible and poison everything downstream.
 *
 * **Why this is the lock the rest of the harvest waits on.** `history::fuseConfidence`
 * computes `1 − (1−p)(1−q)`, and its own header names the word doing the work: INDEPENDENT.
 * Two chroniclers copying the same lost original are one witness, and fusing them as two is
 * the commonest way a corpus manufactures certainty it has not earned. The function cannot
 * check it — nothing local to two numbers can. Something that sees the whole corpus must,
 * and this is that something.
 *
 * So there are two questions here, not one, and they are the same operation applied to two
 * different tables:
 *
 *   - **co-reference** — do these two names denote one entity?
 *   - **independence** — do these two sources constitute one witness?
 *
 * @warning **The rule that keeps this honest: a hard key MERGES, a score only PROPOSES.**
 * Similarity is not transitive and identity is. Merging on a threshold means 'Jean Martin,
 * notary at Rouen' and 'Jean Martin, notary at Rennes' become one man the moment some third
 * spelling sits between them, and the resulting corpus is not noisy — it is confidently
 * wrong, in a way no downstream reader can detect, about a person who now appears to have
 * been in two places at once. Transitive closure is safe over EQUALITY of a normalised
 * reference, because equality is transitive; it is unsafe over resemblance, because
 * resemblance is not. Everything that is merely alike is returned as a CANDIDATE and merged
 * by nobody until something decides. @ref ResolutionParams::autoMergeThreshold exists to let
 * a caller opt into the unsafe thing knowingly, and defaults to off.
 *
 * @warning **And the failure that motivates the second half is not hypothetical here.** A research
 * run cites a preprint as `arxiv.org/abs/2101.01303v1` and the published version as
 * `doi.org/10.1038/…`. Those are one piece of work and two rows, so a claim resting on both
 * looks doubly attested when it is singly attested. Normalising the reference catches the
 * first kind — two spellings of one address — and a declared derivation catches the second.
 *
 * Integer arithmetic throughout, in raw Q16.16 words. Not for the kernel's sake — this
 * module is host-only — but because `include/lpl/Foundation.hpp` refuses to emulate Fixed32
 * in a standalone build, and a similarity that existed only when a sibling checkout did
 * would make half this repository's tests conditional on it.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_ENTITYRESOLUTION_HPP
#    define LPL_LPL_HARVEST_ENTITYRESOLUTION_HPP

#    include <lpl/Foundation.hpp>

#    include <string>
#    include <string_view>
#    include <utility>
#    include <vector>

namespace lpl::harvest {

/// One as a raw Q16.16 word; a similarity of exactly this means the token sets are equal.
inline constexpr core::u32 kSimilarityOne = 65536u;

/**
 * @struct Mention
 * @brief One occurrence of a name, and whatever address came with it.
 */
struct Mention {
    core::u32 id{0u};      ///< The caller's handle for this mention. Returned verbatim.
    std::string name;      ///< How it was written here.
    std::string reference; ///< A URL, DOI or other address, or empty when none came with it.
};

/**
 * @enum Evidence
 * @brief Why two mentions were put together, or proposed to be.
 */
enum class Evidence : core::u32 {
    None = 0u,
    /**
     * Their references normalise to the same string.
     *
     * The only HARD evidence, and the only one merging acts on. It is an equality, so its
     * transitive closure is sound: if a and b are the same address and b and c are, then a
     * and c are, with no threshold anywhere in the argument.
     */
    SameReference = 1u,
    /// Their names resemble each other. Proposes only; see the file header.
    SimilarName = 2u,
    /// One is a strict initialism or abbreviation of the other. Proposes only.
    Abbreviation = 3u,
};

/**
 * @struct Candidate
 * @brief Two mentions that might be one thing, and how strongly.
 */
struct Candidate {
    core::u32 left{0u};      ///< A mention id.
    core::u32 right{0u};     ///< Another, always the larger of the two.
    core::u32 score{0u};     ///< Resemblance in raw Q16.16.
    Evidence evidence{Evidence::None}; ///< What suggested it.
};

/**
 * @struct ResolutionParams
 * @brief What counts as alike, and what a caller is willing to have decided for it.
 */
struct ResolutionParams {
    /**
     * Below this, two names are not even worth proposing. Raw Q16.16; 0.55 by default.
     *
     * A floor on the CANDIDATE list, never on merging. Set low and the list is noise; set
     * high and real pairs never get looked at. It is a reporting threshold, so getting it
     * wrong costs attention rather than correctness — which is the only kind of threshold
     * this file is willing to have.
     */
    core::u32 proposeThreshold{36045u};

    /**
     * Merge anything scoring at least this, without asking. ZERO means never, and that is
     * the default.
     *
     * @warning Turning this on buys throughput with silent errors, and the errors are of the worst
     * available kind: two people becoming one, every claim about either appearing to be a
     * claim about both, and corroboration counting a single witness twice. A caller that
     * sets it is asserting it has a reason this corpus is safe. Nothing here can check that.
     */
    core::u32 autoMergeThreshold{0u};
};

/**
 * @struct Clustering
 * @brief Which mentions ended up denoting the same thing.
 */
struct Clustering {
    /**
     * For each input mention, the id of its cluster's representative.
     *
     * The representative is the SMALLEST member id, always, so the same corpus resolves to
     * the same labels on two machines and in two orders. A representative chosen by
     * insertion order would make the output a fact about the walk rather than about the
     * corpus.
     */
    std::vector<core::u32> representative;
    std::vector<Candidate> candidates; ///< Alike, not merged. Sorted, strongest first.
    core::u32 clusters{0u};            ///< Distinct things, after merging.
    core::u32 merged{0u};              ///< Mentions absorbed into an earlier one.
    core::u32 autoMerged{0u};          ///< Of those, how many on resemblance alone.
    /**
     * Pairs actually scored.
     *
     * The number that says whether the blocking did anything. Against the exhaustive sweep it
     * replaced this is `n(n-1)/2`, which is 500 billion at a million mentions — not a slow
     * answer but no answer at all. Reported so the saving is measured rather than assumed.
     */
    core::u32 comparisons{0u};
    core::u32 blocks{0u};              ///< Distinct blocking tokens indexed.
};

/**
 * @brief Reduces an address to the one string two spellings of it share.
 *
 * Applied in one direction only and never inverted: the output is a comparison key, not a
 * URL anybody should fetch. Each rule below collapses a difference that is known to be
 * cosmetic, and nothing collapses a difference that might not be:
 *
 *   - scheme and case — `HTTPS://Example.ORG/x` and `http://example.org/x`;
 *   - a leading `www.`;
 *   - a DOI however it is dressed: `doi.org/10.1/x`, `dx.doi.org/10.1/x` and a bare
 *     `10.1/x` all become `doi:10.1/x`;
 *   - arXiv's several faces: `/abs/`, `/pdf/`, a `.pdf` suffix and a trailing version
 *     `v3` all become `arxiv:2101.01303`. @warning Dropping the version is a JUDGEMENT, and it is
 *     the right one here: versions of a preprint are one work, and a corpus that treated
 *     them as two would count one author agreeing with themselves;
 *   - a trailing slash, a `#fragment`, and the tracking parameters that identify a click
 *     rather than a document.
 *
 * A query string that is not tracking is KEPT, because for many hosts it is the whole
 * address.
 *
 * @param reference The address as it was written.
 * @return Its canonical form; empty when @p reference is empty or has no usable content.
 */
[[nodiscard]] std::string normaliseReference(std::string_view reference);

/**
 * @brief How alike two names are, as a raw Q16.16 word.
 *
 * Token-set Jaccard over lower-cased alphanumeric runs: the size of the intersection over
 * the size of the union. Chosen over an edit distance because the errors this corpus
 * actually contains are reorderings, honorifics and dropped middle names — 'Louis XIV' and
 * 'Louis XIV of France' — and an edit distance reads those as far apart while reading
 * 'Rouen' and 'Rennes' as close, which is exactly backwards for the job.
 *
 * @warning It has no idea what a word MEANS, so 'The Sun King' and 'Louis XIV' score zero. That is
 * the honest answer and not a defect to be tuned away: nothing about those two strings says
 * they are one man. Collapsing them needs a source that says so, which is what an alias
 * table or a reader is for — and pretending a string metric could do it is how the silent
 * errors this file exists to prevent get made.
 *
 * @param left  One name.
 * @param right The other.
 * @return @ref kSimilarityOne when the token sets are equal, 0 when they are disjoint.
 */
[[nodiscard]] core::u32 nameSimilarity(std::string_view left, std::string_view right) noexcept;

/**
 * @brief Is @p shortForm an initialism or clipping of @p longForm?
 *
 * Answers the one case Jaccard cannot see at all: 'WHO' against 'World Health Organization'
 * share no token, so their similarity is zero and correctly so. Kept strictly separate from
 * the score for that reason, and it PROPOSES like everything else — 'WHO' is also a word.
 *
 * @param shortForm The candidate abbreviation.
 * @param longForm  The candidate expansion.
 * @return true when every letter of @p shortForm is the initial of a word of @p longForm,
 *         in order, and @p shortForm has at least two letters.
 */
[[nodiscard]] bool isAbbreviationOf(std::string_view shortForm, std::string_view longForm) noexcept;

/**
 * @brief The tokens of a name that must be indexed for it to be findable.
 *
 * **Prefix filtering, and it is EXACT rather than a heuristic** — that distinction is the
 * whole reason this is worth writing instead of a cheaper approximate key. Order a name's
 * tokens by how rare they are in the corpus; then for two names to reach a Jaccard similarity
 * of @p threshold, they must share a token within the first `|a| - ceil(t*|a|) + 1` of them.
 * The argument is a counting one: if their shared tokens all sat later than that, too few
 * could be shared for the ratio to reach @p threshold. So indexing only the prefix loses no
 * pair that the exhaustive sweep would have found, which is asserted against the exhaustive
 * sweep rather than argued.
 *
 * @warning Phonetic keys — Soundex, Metaphone — were the obvious alternative and are unusable here:
 * they are built for English surnames, and this corpus is Greek, Latin, Arabic, Sanskrit and
 * Chinese. A key that works on one alphabet is a key that silently blocks nothing on the rest.
 *
 * @param name       The name.
 * @param frequency  Global token frequencies, as built by the caller across the whole batch.
 * @param threshold  The similarity below which a pair is not worth proposing, raw Q16.16.
 * @param out        Receives the prefix tokens; cleared first.
 */
void blockingPrefix(std::string_view name, const std::vector<std::pair<std::string, core::u32>> &frequency,
                    core::u32 threshold, std::vector<std::string> &out);

/**
 * @brief Groups mentions that denote the same thing.
 *
 * @param mentions What was seen.
 * @param params   What counts as alike.
 * @param out      Receives the grouping; cleared first.
 * @return false when two mentions share an id — a caller's handles must be its own, and
 *         silently collapsing two of them would be this function committing the exact error
 *         it exists to prevent.
 */
[[nodiscard]] bool resolveEntities(const std::vector<Mention> &mentions, const ResolutionParams &params,
                                   Clustering &out);

// ─────────────────────────────────────────────────────────────────────────────
// Independence: how many WITNESSES a claim really has
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @struct Derivation
 * @brief A declaration that one source is not independent of another.
 *
 * Declared rather than inferred, and that asymmetry is deliberate. Two sources being the
 * same work can be established from their addresses; one source having COPIED another
 * cannot be, and a harvester that guessed at it would be inventing the very relation the
 * count is supposed to be honest about. What can be observed — a citation, a mirror, a
 * translation, a syndication — is observed by whatever reads the source and is handed here.
 */
struct Derivation {
    core::u32 derived{0u};  ///< The source that owes its content to the other.
    core::u32 original{0u}; ///< The one it owes it to.
};

/**
 * @struct Testimony
 * @brief One source asserting one claim.
 */
struct Testimony {
    core::u32 claim{0u};  ///< Which claim, by the caller's handle.
    core::u32 source{0u}; ///< Which source asserted it.
};

/**
 * @struct AgreementReport
 * @brief How much corroboration a corpus actually contains.
 */
struct AgreementReport {
    /**
     * Per source, in the caller's input order: how many OTHER independent witnesses
     * corroborate the best-corroborated claim it makes.
     *
     * Shaped to drop straight into `history::SourceProfile::independentAgreements`, which is
     * the only reason this number is computed at all.
     */
    std::vector<core::u32> agreements;
    core::u32 witnesses{0u};   ///< Distinct independent witnesses among the sources.
    core::u32 collapsed{0u};   ///< Sources that turned out not to be their own witness.
    core::u32 corroborated{0u}; ///< Claims genuinely attested by more than one witness.
    core::u32 inflated{0u};    ///< Claims that LOOKED corroborated and were not. See below.
};

/**
 * @brief Counts how many independent witnesses stand behind each claim.
 *
 * Two sources are one witness when their references normalise alike, or when a @ref
 * Derivation says one came from the other. Everything else is taken to be independent,
 * which is the assumption `fuseConfidence` was already making silently — the difference is
 * that here it is an assumption a corpus can be measured against.
 *
 * @ref AgreementReport::inflated is the number this function exists to produce: claims that
 * several sources assert and only one witness attests. That count is zero for a corpus with
 * no duplicate sources, so a zero means one of two very different things — no duplication,
 * or no detection — and a caller that reports it should say which corpus it ran on.
 *
 * @param sources     The source ids, in the order the caller wants results back.
 * @param references  One address per source, index-aligned; empty entries are simply
 *                    unaddressed and are never equal to one another.
 * @param testimonies Who asserts what.
 * @param derivations Known dependencies between sources.
 * @param out         Receives the tally; cleared first.
 * @return false when @p references is not the same length as @p sources, or when a source id
 *         appears twice.
 */
[[nodiscard]] bool countIndependentAgreements(const std::vector<core::u32> &sources,
                                              const std::vector<std::string> &references,
                                              const std::vector<Testimony> &testimonies,
                                              const std::vector<Derivation> &derivations,
                                              AgreementReport &out);

} // namespace lpl::harvest

#endif // LPL_LPL_HARVEST_ENTITYRESOLUTION_HPP
