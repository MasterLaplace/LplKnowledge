/**
 * @file History.hpp
 * @brief The bridge: what an image holds, as what `lpl::history` weighs.
 *
 * This file exists because of a division of labour that `history/Fact.hpp` wrote down
 * itself: "Subject, predicate and object are IDENTIFIERS, not strings. The strings live
 * in LplKnowledge, which is where a corpus is curated." So `history/` owns the ARITHMETIC
 * of doubt — trust, fusion, the demotion of a contradicted claim — and this repository
 * owns the corpus, the format and the identity. Neither re-implements the other, and
 * `graph/FOLDED.md` records the module that was about to.
 *
 * The direction of the dependency is deliberate and one-way: LplKnowledge knows about
 * LplPlugin, never the reverse. LplPlugin must keep building with no knowledge checkout
 * present, exactly as it builds with no kernel — so the adapter lives on this side, under
 * `LPL_HAS_FOUNDATION`, the same arrangement `mind/` uses for the decision seam.
 *
 * Only RECORD conversion is here, never image assembly. Building an image needs a heap
 * and a growing buffer, which is `harvest::Baker`'s business on the hosted side; putting it
 * here would have made `knowledge/` depend on `harvest/` and pointed the reader half at the
 * writer half.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_KNOWLEDGE_HISTORY_HPP
#    define LPL_LPL_KNOWLEDGE_HISTORY_HPP

#    include <lpl/Foundation.hpp>

#    if defined(LPL_HAS_FOUNDATION)

#        include <lpl/history/PossibleWorld.hpp>
#        include <lpl/knowledge/KnowledgePack.hpp>

namespace lpl::knowledge {

// The wire enumeration mirrors history::SourceKind by VALUE, because this module has to
// compile with no LplPlugin on the include path. Here, where both are visible, the mirror
// is asserted: reordering either side then fails a build instead of silently reinterpreting
// every source in every image already written — a panegyric read as a notarial act would
// change which of two contradictory claims becomes the consensus.
static_assert(static_cast<core::u32>(SourceKindV1::Notarial) == static_cast<core::u32>(history::SourceKind::Notarial),
              "SourceKindV1 must mirror history::SourceKind");
static_assert(static_cast<core::u32>(SourceKindV1::Archaeology) ==
                  static_cast<core::u32>(history::SourceKind::Archaeology),
              "SourceKindV1 must mirror history::SourceKind");
static_assert(static_cast<core::u32>(SourceKindV1::Administrative) ==
                  static_cast<core::u32>(history::SourceKind::Administrative),
              "SourceKindV1 must mirror history::SourceKind");
static_assert(static_cast<core::u32>(SourceKindV1::Chronicle) == static_cast<core::u32>(history::SourceKind::Chronicle),
              "SourceKindV1 must mirror history::SourceKind");
static_assert(static_cast<core::u32>(SourceKindV1::Panegyric) == static_cast<core::u32>(history::SourceKind::Panegyric),
              "SourceKindV1 must mirror history::SourceKind");
static_assert(static_cast<core::u32>(SourceKindV1::Count) == static_cast<core::u32>(history::SourceKind::Count),
              "SourceKindV1 must mirror history::SourceKind");

/**
 * @struct DecodeReport
 * @brief What decoding an image into a corpus had to reject.
 */
struct DecodeReport {
    core::u32 facts{0u};        ///< Claims admitted.
    core::u32 sources{0u};      ///< Source profiles admitted.
    core::u32 badKind{0u};      ///< Sources naming a kind this build does not know. REJECTED.
    core::u32 badWindow{0u};    ///< Claims whose window ends before it starts. REJECTED.
    core::u32 badConfidence{0u}; ///< Claims whose confidence is outside [0,1]. REJECTED.
};

/**
 * @brief Turns one wire claim into one the arithmetic can weigh.
 *
 * Rejects rather than clamps, and that is the whole difference between a reader and a
 * guesser. A confidence above one would make `1 - (1-p)(1-q)` go backwards under fusion,
 * so a claim carrying one is not a claim held very firmly — it is a corrupt record, and
 * silently pulling it down to one would let it outvote every honest source in the image.
 *
 * @param wire The record as the image holds it.
 * @param out  Receives the fact.
 * @return false when the record cannot be believed as written.
 */
[[nodiscard]] bool fromWireFact(const FactV1 &wire, history::Fact &out) noexcept;

/**
 * @brief Turns one fact into the record an image holds.
 *
 * @param fact  What to encode.
 * @param locus One-based index into the Loci section, or @ref kNoIdentifier.
 * @param out   Receives the record.
 */
void toWireFact(const history::Fact &fact, core::u32 locus, FactV1 &out) noexcept;

/**
 * @brief Turns one wire source profile into one the trust score can read.
 *
 * @param wire The record as the image holds it.
 * @param out  Receives the profile.
 * @return false when the record names a kind this build does not know.
 */
[[nodiscard]] bool fromWireSource(const SourceV1 &wire, history::SourceProfile &out) noexcept;

/**
 * @brief Turns one source profile into the record an image holds.
 *
 * @param profile  What to encode.
 * @param name     Identifier whose vocabulary entry names it, or @ref kNoIdentifier.
 * @param document One-based index into the Documents section, or @ref kNoIdentifier.
 * @param out      Receives the record.
 */
void toWireSource(const history::SourceProfile &profile, core::u32 name, core::u32 document,
                  SourceV1 &out) noexcept;

/**
 * @brief Decodes a whole image into a corpus.
 *
 * @param pack   The image.
 * @param out    Receives the corpus; cleared first.
 * @param report Receives what was rejected.
 * @return false when anything was rejected — a corpus assembled from an image that had
 *         records this build could not read is NOT the corpus that was baked, and treating
 *         it as one is how a missing claim becomes an argument about history.
 */
[[nodiscard]] bool toHistoryCorpus(const KnowledgePack &pack, history::Corpus &out, DecodeReport &report);

/**
 * @brief Are two corpora the same, field for field?
 *
 * The round-trip check, and it compares FIELDS rather than folds on purpose: two folds
 * being equal says the two corpora agree, and comparing fields says so as well while
 * being able to stop at the first that does not. A fold is the cross-target check; this is
 * the same-machine one.
 *
 * @param a First corpus.
 * @param b Second corpus.
 * @return true when both hold the same claims and the same sources, in the same order.
 */
[[nodiscard]] bool corporaMatch(const history::Corpus &a, const history::Corpus &b) noexcept;

} // namespace lpl::knowledge

#    endif // LPL_HAS_FOUNDATION

#endif // LPL_LPL_KNOWLEDGE_HISTORY_HPP
