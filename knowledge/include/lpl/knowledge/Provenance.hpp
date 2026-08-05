/**
 * @file Provenance.hpp
 * @brief Who asserted this, and how we came to hold it.
 *
 * Provenance is not metadata here, it is the primary key of the epistemology: a
 * fact without a source cannot be weighed, therefore cannot be believed.
 *
 * That sentence has a consequence the rest of this file exists to enforce. If a claim
 * whose source is not described cannot be believed, then an image carrying one is
 * BROKEN, not merely incomplete — and the break is silent, because such a claim scores
 * whatever the default trust weights happen to yield and then competes with properly
 * sourced claims on equal footing. So @ref auditProvenance names the condition, the baker
 * refuses to write it, and the reader can report it.
 *
 * The distinction the audit draws, and it is the one that matters: a claim with no LOCUS
 * is legitimate — plenty of knowledge is held without a line number — while a claim
 * whose locus points past the end of the loci section is a defect. Absent and dangling
 * are counted apart, because one is a corpus that is honest about what it does not know
 * and the other is an image that has lost part of itself.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_KNOWLEDGE_PROVENANCE_HPP
#    define LPL_LPL_KNOWLEDGE_PROVENANCE_HPP

#    include <lpl/Foundation.hpp>
#    include <lpl/knowledge/KnowledgePack.hpp>

namespace lpl::knowledge {

/**
 * @struct Citation
 * @brief Everything an image can say about where one claim came from.
 */
struct Citation {
    SourceV1 source{};          ///< Who asserted it.
    DocumentV1 document{};      ///< The work, when the source names one.
    LocusV1 locus{};            ///< Where in it, when the claim names one.
    const char *sourceName{nullptr};   ///< Vocabulary text for the source, or nullptr.
    const char *documentName{nullptr}; ///< Vocabulary text for the work, or nullptr.
    core::u32 hasDocument{0u};  ///< 1 when @c document is filled.
    core::u32 hasLocus{0u};     ///< 1 when @c locus is filled.
};

/**
 * @brief Resolves where a claim came from.
 *
 * @param pack The image the claim was read from.
 * @param fact The claim.
 * @param out  Receives the citation.
 * @return false when the image does not describe the claim's source — which makes the
 *         claim unweighable, and is therefore a failure and not an empty result.
 */
[[nodiscard]] bool cite(const KnowledgePack &pack, const FactV1 &fact, Citation &out) noexcept;

/**
 * @brief Writes a citation in a form a person can check.
 *
 * @param citation What to render.
 * @param out      Receives a NUL-terminated line.
 * @param capacity Room in @p out, NUL included.
 * @return Bytes written, NUL excluded.
 */
[[nodiscard]] core::u32 renderCitation(const Citation &citation, char *out, core::u32 capacity) noexcept;

/**
 * @struct ProvenanceAudit
 * @brief What an image's provenance is missing, told apart by kind.
 */
struct ProvenanceAudit {
    core::u32 facts{0u};            ///< Claims examined.
    core::u32 unsourced{0u};        ///< Claims whose source the image does not describe. A DEFECT.
    core::u32 unlocated{0u};        ///< Claims that name no locus. Legitimate.
    core::u32 danglingLocus{0u};    ///< Claims whose locus index leaves the section. A DEFECT.
    core::u32 danglingDocument{0u}; ///< Loci or sources pointing past the documents. A DEFECT.
    core::u32 unnamed{0u};          ///< Sources with no vocabulary entry. Legitimate.
};

/**
 * @brief Walks every claim and tallies what its provenance is missing.
 *
 * @param pack The image.
 * @param out  Receives the tally.
 * @return true when no DEFECT was found — that is, when every claim can be weighed.
 */
[[nodiscard]] bool auditProvenance(const KnowledgePack &pack, ProvenanceAudit &out) noexcept;

/**
 * @brief Folds an audit into a signature.
 *
 * @param audit What to fold.
 * @return The signature.
 */
[[nodiscard]] core::u32 foldAudit(const ProvenanceAudit &audit) noexcept;

} // namespace lpl::knowledge

#endif // LPL_LPL_KNOWLEDGE_PROVENANCE_HPP
