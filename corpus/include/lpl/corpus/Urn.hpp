/**
 * @file Urn.hpp
 * @brief Canonical text identifiers (CTS URNs and friends).
 *
 * Every line of the classical corpora already carries a stable URN. Adopting the
 * existing scheme rather than minting our own is the difference between citing
 * the world's scholarship and forking it.
 *
 * This file is the other end of a seam `history/Fact.hpp` opened deliberately: "Subject,
 * predicate and object are IDENTIFIERS, not strings. The strings live in LplKnowledge."
 * So the mapping from a canonical name to the 32-bit word the engine trades in is here,
 * and only here.
 *
 * ⚠ **The mapping is a 32-bit hash, therefore it collides**, and that is stated up front
 * because the failure mode is silent and severe: two colliding URNs would merge two people
 * into one, and every claim about either would appear to be a claim about both. A caller
 * that interns a corpus MUST detect collisions and refuse — @ref workIdentifier cannot,
 * because it sees one name at a time. `harvest::Baker` is where that detection lives, and
 * it refuses to bake rather than merging.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_CORPUS_URN_HPP
#    define LPL_LPL_CORPUS_URN_HPP

#    include <lpl/Foundation.hpp>
#    include <lpl/corpus/Locus.hpp>

namespace lpl::corpus {

/**
 * @struct Urn
 * @brief The parsed components of a CTS URN.
 *
 * Views into the caller's buffer, never copies: a URN is read out of a document that is
 * already in memory, and copying it would be a heap allocation on the one path that must
 * not have one.
 */
struct Urn {
    const char *namespaceText{nullptr}; ///< The CTS namespace, e.g. "greekLit".
    core::u32 namespaceBytes{0u};
    const char *workText{nullptr}; ///< The work component, e.g. "tlg0016.tlg001.perseus-grc2".
    core::u32 workBytes{0u};
    const char *passageText{nullptr}; ///< The passage, e.g. "1.5"; empty when absent.
    core::u32 passageBytes{0u};
};

/**
 * @brief Parses a CTS URN.
 *
 * Accepts `urn:cts:<namespace>:<work>` with an optional `:<passage>`. Anything else is
 * refused: a scheme this parser does not understand must not be silently treated as a
 * work name, or two different documents end up with the same identity.
 *
 * @param text  First byte of the URN.
 * @param bytes Its length.
 * @param out   Receives the components.
 * @return false when the string is not a CTS URN.
 */
[[nodiscard]] bool parseUrn(const char *text, core::u32 bytes, Urn &out) noexcept;

/**
 * @brief The identifier of the WORK a URN names, passage excluded.
 *
 * Passage excluded deliberately, and this is the modelling decision the format rests on:
 * every line of a work shares one document identifier, and where in it a claim was found
 * is a @ref Locus. The alternative — one identifier per cited line — would make two claims
 * from the same chronicle look like claims from two different chronicles, and source
 * corroboration would count them as independent.
 *
 * @param urn A parsed URN.
 * @return The identifier; never zero, since zero means "none".
 */
[[nodiscard]] core::u32 workIdentifier(const Urn &urn) noexcept;

/**
 * @brief The identifier of an arbitrary canonical name.
 *
 * For the names that are not CTS URNs: a subject, a predicate, a working document. Same
 * derivation, so a name interned twice in two tools yields the same word.
 *
 * @param text  First byte of the name.
 * @param bytes Its length.
 * @return The identifier; never zero.
 */
[[nodiscard]] core::u32 nameIdentifier(const char *text, core::u32 bytes) noexcept;

/**
 * @brief Parses a URN and splits it into a work identifier and a locus.
 *
 * The one call a harvester actually wants, so the two halves cannot be paired wrongly.
 *
 * @param text     First byte of the URN.
 * @param bytes    Its length.
 * @param outWork  Receives the work identifier.
 * @param outLocus Receives the passage, zeroed when the URN names no passage.
 * @return false when the string is not a CTS URN, or its passage is malformed.
 */
[[nodiscard]] bool resolveUrn(const char *text, core::u32 bytes, core::u32 &outWork, Locus &outLocus) noexcept;

} // namespace lpl::corpus

#endif // LPL_LPL_CORPUS_URN_HPP
