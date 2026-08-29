/**
 * @file Mentions.hpp
 * @brief What a primary text names, where it names it, and when it says it happened.
 *
 * @warning **This exists because an aggregator is not a witness.** The obvious way to get a corpus of
 * deeds is to take one from a database that already has them -- but a database of that kind
 * recopies its sources rather than seeing anything, so every claim in it shares one provenance.
 * `fuseConfidence` states the hazard in its own header: two chroniclers copying a lost original
 * are ONE source, and the function cannot detect it. Importing thousands of claims under a single
 * source would make `independentAgreements` meaningless at best and inflated at worst.
 *
 * So the material comes from the texts themselves. Measured across the Perseus corpus: **40 449**
 * `<placeName>`, **13 564** `<persName>` and **12 644** `<date>` elements, of which **38 848**
 * place mentions carry an editor's authority key. Each sits at an exact CTS citation, so anything
 * built on one is checkable by opening the passage -- which is the whole difference between a
 * fact and a rumour with a footnote.
 *
 * @warning **A mention is a fact about the TEXT, not about the world.** "Herodotus names Aetna at
 * 1.2" is not "Aetna existed in the fifth century": the first is observable and the second is an
 * inference somebody has to argue for. Keeping them apart is why the predicates here are
 * `names-place` and `names-person` rather than anything that sounds like existence -- a reader
 * who wants the second must combine mentions with something else and say so.
 *
 * @warning **An authority key FUSES, a bare name only proposes.** `key="tgn,7003867"` is an editor
 * stating that this word denotes that entry of the Getty Thesaurus; it is evidence, and the same
 * hard-key rule `EntityResolution` follows applies. The 1601 place mentions with no key are
 * counted and left as names, because "Alexandria" names ten different places and matching on the
 * word would put the library in Afghanistan.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_MENTIONS_HPP
#    define LPL_LPL_HARVEST_MENTIONS_HPP

#    include <lpl/Foundation.hpp>

#    include <string>
#    include <string_view>

namespace lpl::harvest {

/**
 * @struct AuthorityKey
 * @brief An editor's `key` attribute, split into the authority and the entry.
 */
struct AuthorityKey {
    std::string_view authority; ///< `tgn`, `perseus`, `tlg-0138`, ...
    std::string_view entry;     ///< Whatever follows the comma, verbatim.
    bool present{false};        ///< Whether there was a key at all.
};

/**
 * @brief Splits a TEI `key` attribute.
 *
 * @warning Split on the FIRST comma only. Measured shapes include `tgn,7003867` and
 * `perseus,Aetna`, and an entry is free to contain further commas; consuming them all would
 * truncate the entry of whichever editor used one.
 *
 * @param key The attribute value.
 * @return The parts; @c present is false for an empty or comma-less value, which is a name
 *         without an authority rather than a malformed key.
 */
[[nodiscard]] AuthorityKey splitAuthorityKey(std::string_view key) noexcept;

/**
 * @brief The identifier a keyed mention resolves to.
 *
 * @warning Built from the WHOLE key, authority included, because two authorities number their
 * entries independently: `tgn,7003867` and `perseus,7003867` are not the same place and folding
 * them together would merge two entities on a coincidence of digits.
 *
 * @param key The split key.
 * @return The identifier, or @c 0 when the key is absent.
 */
[[nodiscard]] core::u32 authorityIdentifier(const AuthorityKey &key) noexcept;

/**
 * @struct DateWindow
 * @brief When a `<date>` says something happened, as an interval.
 */
struct DateWindow {
    core::i32 fromDay{0}; ///< First day covered.
    core::i32 toDay{0};   ///< Last day covered.
    bool known{false};    ///< Whether any of the attributes could be read.
};

/**
 * @brief Reads a TEI date string into a day window.
 *
 * @warning **The precision is the WIDTH, and the corpus states it three different ways.** Measured
 * shapes: `-0480` (a whole year, 3841 of them), `1863-05` (a whole month, 103), `1863-05-01` (one
 * day, 941). A field in years could carry only the first, so an editor who took the trouble to
 * date something to the day had that work discarded at ingestion. Here a bare year widens to its
 * 1 January -- 31 December, a year-month to that month's first and last, and a full date to
 * itself.
 *
 * @warning Leading zeroes and a leading minus are both significant and both common: `-0480` is 480
 * BCE. A parser that treated the minus as a separator would read it as 480 CE, which is the same
 * number in the wrong millennium.
 *
 * @param text The attribute value.
 * @param out  Receives the window.
 * @return false when nothing datable could be read, which is different from a date of zero.
 */
[[nodiscard]] bool parseTeiDate(std::string_view text, DateWindow &out) noexcept;

/**
 * @brief Reads the date attributes of one `<date>` element into a single window.
 *
 * @warning TEI offers several ways to say when, and they are not synonyms: `when` is a point at the
 * stated precision, `from`/`to` is a span the thing actually covered, and `notBefore`/`notAfter`
 * is a span the EVIDENCE allows. This collapses all three to an interval, which is lossy about
 * the distinction and exact about the extent -- and the extent is what every consumer here needs.
 * The distinction is worth a field the day something consumes it, and not before.
 *
 * @param tag The whole `<date …>` start tag.
 * @param out Receives the window.
 * @return false when the element carries no readable date.
 */
[[nodiscard]] bool dateWindowOf(std::string_view tag, DateWindow &out) noexcept;

} // namespace lpl::harvest

#endif // LPL_LPL_HARVEST_MENTIONS_HPP
