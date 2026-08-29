/**
 * @file ResearchReport.hpp
 * @brief Reading a deep-research run into the library.
 *
 * The measurement that produced this file, before a line of it was written: a REAL research
 * run — `research_runs/20260716-…-crispr-cas9-…/report.md`, eleven kilobytes, seven cited
 * sources, twenty extracted findings — was dropped into a harvested root and ingested. It
 * baked to **0 facts**. The librarian recorded that a document exists and knew nothing
 * whatever about what it said.
 *
 * The reason is not a missing pipe. `ingestMarkdown` reads STRUCTURAL IDENTIFIERS — the
 * `SIM-016` scheme this project's own working documents use — and a research report has
 * none. The librarian accepted exactly one kind of matter, and a report is a different kind.
 *
 * **What makes this readable without a model, when prose is not.** A research report is not
 * arbitrary text: it is written by a known writer, `lpl::research::writeReport`, whose shape
 * is a contract rather than a guess. Two parts of that shape are machine-readable and the
 * rest is not, and the split is the same one @ref Markdown.hpp draws:
 *
 *   - the **graph** — which sources a run consulted, which it actually read, which finding
 *     rests on which source, what the run was about, when it ran — is stated structurally by
 *     the writer, so it is derived here and is never wrong for long;
 *   - the **text** — the wording of a finding — is authored by a model at research time and
 *     travels verbatim in the image's `Texts` section, so it survives with its provenance.
 *
 * @warning **The findings are not extracted from the prose, and that distinction is the whole
 * point.** `RunState::knowledge` already holds them as `[S<id>] claim`, tagged with the
 * source that backs each one, because the research engine tagged them when it extracted
 * them. `writeReport` used to dissolve that list into paragraphs and drop it. So the missing
 * link was never a second model re-reading the first model's prose — it was a writer
 * throwing structure away. Re-deriving it here would be this repository's most familiar
 * mistake: the same knowledge computed twice, by two methods free to disagree.
 *
 * @warning **Retrieval, not adjudication** — the same limit @ref Markdown.hpp names, for the same
 * reason. `history::contradicts` assumes a FUNCTIONAL predicate: one object per subject per
 * window, because a person is not in two places in one year. A report legitimately states
 * many findings and a source legitimately backs many of them, so the mutual-exclusion rule
 * fires on every pair and means nothing when it does. A consensus computed over a pile of
 * reports is not so much wrong as empty, and the tools say so rather than printing a number
 * that reads like a verdict.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_RESEARCHREPORT_HPP
#    define LPL_LPL_HARVEST_RESEARCHREPORT_HPP

#    include <lpl/Foundation.hpp>
#    include <lpl/harvest/Baker.hpp>

#    include <string>
#    include <string_view>
#    include <vector>

namespace lpl::harvest {

/**
 * The predicates a research run can assert.
 *
 * Numbered apart from the markdown reader's 1001–1003 rather than renumbered alongside them:
 * an identifier travels verbatim into every image, so a predicate that changed value would
 * silently reinterpret every `.lplknow` already on disk. New meaning, new number.
 *
 * @warning None of these is FUNCTIONAL either. See the file header.
 */
enum : core::u32 {
    kPredicateStatedIn = 1101u,    ///< This finding is stated in this report. NOT functional.
    kPredicateRestsOn = 1102u,     ///< This finding rests on this source. NOT functional.
    kPredicateFindingText = 1103u, ///< The verbatim words of this finding are line N of Texts.
    kPredicateSourceUrl = 1104u,   ///< Where this source is; the text is line N of Texts.
    kPredicateResearches = 1105u,  ///< What this report set out to answer; line N of Texts.
    /**
     * This report confirmed this source reachable, in the year it ran.
     *
     * Emitted only for a source the report marked reachable. A report marks three states and
     * only one of them is evidence of anything durable: a failed HEAD may be a transient 503,
     * an anti-bot page or a network that was down for the whole gate pass — the writer itself
     * detects that last case and marks every source `~`. Baking "this URL is dead" from one
     * request would be a claim the evidence does not support, so absence here means NOT
     * CONFIRMED, never CONFIRMED BROKEN. That is the distinction `Provenance.hpp` already
     * insists on between a legitimate absence and a defect.
     */
    kPredicateReachable = 1106u,
};

/**
 * @struct ResearchDocument
 * @brief One report to read.
 */
struct ResearchDocument {
    std::string path;      ///< Where it is on disk.
    std::string canonical; ///< How it should be cited — a path relative to the corpus root.
};

/**
 * @struct ResearchIngestReport
 * @brief What reading a set of runs found.
 */
struct ResearchIngestReport {
    core::u32 reports{0u};       ///< Reports read.
    core::u32 lines{0u};         ///< Lines scanned.
    core::u32 findings{0u};      ///< Findings carrying a resolvable source tag.
    core::u32 orphanFindings{0u}; ///< Findings whose `[S<id>]` no source list resolves. See below.
    core::u32 sources{0u};       ///< Distinct sources across every report, by URL.
    core::u32 unreadable{0u};    ///< Sources the run listed but could not fetch.
    core::u32 reachable{0u};     ///< Sources the run confirmed reachable.
    core::u32 citations{0u};     ///< Inline `[S<id>]` references from the prose.
    core::u32 dated{0u};         ///< Reports whose byline carried a date.
    core::u32 witnesses{0u};     ///< Distinct INDEPENDENT witnesses among those sources.
    core::u32 collapsed{0u};     ///< Sources that turned out to be a spelling of another.
    core::u32 corroborated{0u};  ///< Findings genuinely attested by more than one witness.
    /**
     * Findings that LOOKED corroborated and were not.
     *
     * Several sources assert it; one witness attests it. This is the number the whole
     * independence apparatus exists to produce, because `history::fuseConfidence` would
     * otherwise raise a confidence on the strength of one document cited twice.
     *
     * @warning A zero means one of two very different things — no duplication in this corpus, or
     * no detection — so a tool reporting it should say which corpus it ran on.
     */
    core::u32 inflated{0u};
    std::string firstOrphan;     ///< The first unresolved tag, for the message.
};

/**
 * @brief Does this document look like a Laplace research report?
 *
 * Keyed on the writer's byline — `*Rapport Laplace deep research — …*` — because that line
 * has been in every report this engine has ever written and names the writer explicitly.
 *
 * @warning Content-based discrimination is a trap this project has already paid for once: the
 * satellite protocol told text from audio by a four-byte prefix, and `TXT:` is two perfectly
 * ordinary loud samples, so a loud enough reply was silently displayed instead of played.
 * The escape there was a header, and it is worth saying why one is not needed here: a
 * distinctive full phrase in a document that is markdown either way cannot be mistaken for
 * the payload, because there is no payload — every candidate is already text, and the worst
 * case of a false positive is a document read by a reader that finds nothing in it and says
 * so. The failure is loud and bounded, which is exactly what the audio case was not.
 *
 * @param body  The document's bytes.
 * @return true when a research reader should handle it.
 */
[[nodiscard]] bool looksLikeResearchReport(std::string_view body) noexcept;

/**
 * @brief Reads a set of research reports into a baker.
 *
 * @param documents What to read.
 * @param baker     Where to put it.
 * @param outReport Receives the tally.
 * @return false when a document could not be opened, or when two canonical names collide —
 *         a corpus that silently omits a file it was asked to read is a corpus whose counts
 *         mean nothing.
 */
[[nodiscard]] bool ingestResearchReports(const std::vector<ResearchDocument> &documents, Baker &baker,
                                         ResearchIngestReport &outReport);

/**
 * @struct ProviderProfile
 * @brief What a search provider's answers are worth, and what kind of thing they are.
 */
struct ProviderProfile {
    knowledge::SourceKindV1 kind; ///< Which epistemic posture it maps onto.
    core::u32 confidenceRaw;      ///< Base confidence of a finding it backs, raw Q16.16.
};

/**
 * @brief What kind of source a provider yields, and how much it is worth believing.
 *
 * **These numbers are written, not derived, and that is deliberate.** `history::TrustWeights`
 * takes the same position for the same reason: which sources are worth more is a
 * historiographical claim, not a fact, and a project that computes it from an enumeration
 * index has taken the position without saying so. Written here, in one place, they can be
 * argued with.
 *
 * @warning **The mapping onto `SourceKindV1` is an ANALOGY and should be read as one.** That
 * enumeration was written for archives — notarial acts, chronicles, panegyrics — and the
 * axis it actually encodes is not medium but posture: it runs from writing meant to be
 * checked to writing meant to persuade. A peer-reviewed paper is `Notarial` on exactly that
 * reading and on no other: it is a document produced to be verified by people whose job is
 * to try. A preprint is the same work before anybody tried, which is a contemporary account
 * — honest and partial — so `Chronicle`. Extending the enumeration instead was considered
 * and rejected: its values are mirrored by `history::SourceKind` under a `static_assert`,
 * and every image on disk already encodes them, so a sixth kind is a wire-format change to
 * settle a question the existing axis already answers.
 *
 * @param provider The provider name as the report records it.
 * @return Its profile; the unknown-provider profile for anything unrecognised.
 */
[[nodiscard]] ProviderProfile providerProfile(std::string_view provider) noexcept;

} // namespace lpl::harvest

#endif // LPL_LPL_HARVEST_RESEARCHREPORT_HPP
