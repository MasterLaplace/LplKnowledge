/**
 * @file Parity.hpp
 * @brief The corpus both sides read, and the round trip that has to lose nothing.
 *
 * The gate: the same pack queried on the host oracle and in ring 0 folds the same
 * signature.
 *
 * Gate **P18 `corpus`**, and its claim is unusual enough to be worth stating precisely,
 * because it is what distinguishes it from every gate before it. The others fold a
 * computation and check that two targets compute it identically. This one folds a
 * TRANSLATION: the canonical corpus of gate P13 is baked into a `.lplknow` image, read
 * back out, and run through the same pipeline — and the three signatures it produces must
 * equal P13's own, on the host AND in ring 0.
 *
 * That is a stronger statement than "the image parses". A format can round-trip a corpus
 * into something that opens cleanly, answers plausibly, and has quietly rounded one
 * confidence — and the consequence would be a different consensus about how a king died,
 * which is exactly the failure this whole module exists to make impossible. Equality with
 * P13 is therefore not decoration; it is the only check that could notice.
 *
 * @warning The corpus is declared ONCE, in `history::parityCorpus`. Nothing here re-states it.
 * Two transcriptions of the same corpus would make this a test of transcription.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_KNOWLEDGE_PARITY_HPP
#    define LPL_LPL_KNOWLEDGE_PARITY_HPP

#    include <lpl/Foundation.hpp>

#    if defined(LPL_HAS_FOUNDATION)

#        include <lpl/knowledge/KnowledgePack.hpp>

namespace lpl::knowledge {

/**
 * @struct KnowledgeFoldResult
 * @brief The signatures the kernel must reproduce.
 *
 * Plain words only, no Fixed32 and no bool, so the kernel copies it field by field
 * exactly as it does for every other fold result.
 */
struct KnowledgeFoldResult {
    core::u32 imageSignature{0u};      ///< Fold of the image, byte for byte.
    core::u32 factSignature{0u};       ///< Fold of every decoded claim.
    core::u32 vocabularySignature{0u}; ///< Fold of the names the image carries.
    core::u32 auditSignature{0u};      ///< Fold of the provenance tally.
    core::u32 pageSignature{0u};       ///< Fold of the canonical query's page.
    core::u32 citationSignature{0u};   ///< Fold of the rendered citation of the cited claim.

    /**
     * The three signatures that must EQUAL gate P13.
     *
     * Named the same as there on purpose: a reader comparing two logs should not have to
     * work out which field corresponds to which.
     */
    core::u32 timelineSignature{0u};
    core::u32 chronicleSignature{0u};
    core::u32 minoritySignature{0u};

    core::u32 imageBytes{0u};   ///< Size of the image read.
    core::u32 openStatus{0u};   ///< An @ref OpenStatus value; 0 when the image was accepted.
    core::u32 sections{0u};     ///< Sections the table declared.
    core::u32 skipped{0u};      ///< Sections whose type this reader does not know.
    core::u32 facts{0u};        ///< Claims decoded.
    core::u32 sources{0u};      ///< Source profiles decoded.
    core::u32 documents{0u};    ///< Documents the image describes.
    core::u32 loci{0u};         ///< Loci the image describes.
    core::u32 vocabulary{0u};   ///< Identifiers the image names.
    core::u32 queryMatched{0u}; ///< Claims the canonical query matched.
    core::u32 queryReturned{0u}; ///< Rows it returned under its cap.
    core::u32 queryTruncated{0u}; ///< 1 when the cap bit.
    core::u32 consensusObject{0u}; ///< What the decoded corpus believes killed the king.
    core::u32 provenanceOk{0u};  ///< 1 when every claim can be weighed.
    core::u32 roundTrip{0u};     ///< 1 when the decoded corpus equals the authored one, field for field.
    core::u32 decodeRejected{0u}; ///< Records the decoder refused. MUST be zero.
};

/**
 * @brief Reads an image, runs the pipeline on it, and folds every stage.
 *
 * One function, called by the host oracle and by the kernel smoke. Takes the image as
 * BYTES rather than reaching for a file or a symbol, so the same code serves a host that
 * just baked one and a kernel that has one in BSS.
 *
 * @param image First byte of the image.
 * @param size  How many bytes.
 * @param out   Receives the signatures.
 */
void foldKnowledgeState(const core::u8 *image, core::u32 size, KnowledgeFoldResult &out);

/**
 * @brief The canonical query, in one place.
 *
 * A named function rather than a query spelled out in two smokes, for the reason the whole
 * project keeps re-learning: two spellings of one question are two questions as soon as one
 * of them is edited.
 *
 * @return What the gate asks the image.
 */
[[nodiscard]] core::u32 parityQuerySubject() noexcept;

} // namespace lpl::knowledge

#    endif // LPL_HAS_FOUNDATION

#endif // LPL_LPL_KNOWLEDGE_PARITY_HPP
