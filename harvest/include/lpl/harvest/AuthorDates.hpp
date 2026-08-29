/**
 * @file AuthorDates.hpp
 * @brief When an author lived, joined across two catalogues that share no identifier.
 *
 * @warning **The soft join, and the rule that makes it admissible.** A text corpus names "Herodotus"
 * and a bibliographic one names "Herodotus", and no identifier connects them -- so pairing them is
 * a guess about identity, which is the one guess this project refuses to let act on its own. What
 * it may do is fill a VALUE and say where the value came from.
 *
 * The split follows from what is reversible:
 *  - an **exactly matching** normalised name fills the composition window and sets
 *    `kSourceFlagWindowFromNameMatch`, so a caller can keep or drop every soft-joined record;
 *  - a merely **similar** name fills nothing and records a @ref Candidate, because a score
 *    proposes and never decides.
 *
 * Neither ever merges two records. Splitting is reversible -- two entries for one person can be
 * joined the day evidence arrives -- while merging destroys the distinction itself, and no later
 * filter recovers it. Measured on Pleiades, the same hazard on places: 1170 names are borne by
 * more than one place, and fourteen of them begin with "Alexandri", from Egypt to Tajikistan.
 *
 * @warning An exact name match is still not an identity. Two men are called Jean Martin, one a notary
 * at Rouen and one at Rennes, and this will happily give both the same window. That is why the
 * flag exists rather than being thought unnecessary: the window is a hypothesis, marked as one.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_AUTHORDATES_HPP
#    define LPL_LPL_HARVEST_AUTHORDATES_HPP

#    include <lpl/Foundation.hpp>

#    include <string>
#    include <string_view>
#    include <vector>

namespace lpl::harvest {

class Baker;

/**
 * @struct AuthorWindow
 * @brief One author, and the years in which they could have written.
 */
struct AuthorWindow {
    std::string name;    ///< As the source catalogue spells it.
    core::i32 fromDay{0}; ///< Earliest, as a day; 0 when unknown.
    core::i32 toDay{0};   ///< Latest; 0 when unknown.
};

/**
 * @struct AuthorDateMatch
 * @brief What a lookup found, and how sure it is.
 */
struct AuthorDateMatch {
    core::i32 fromDay{0};  ///< Filled only on an exact normalised match.
    core::i32 toDay{0};    ///< Likewise.
    core::u32 scoreRaw{0}; ///< Resemblance of the best candidate, raw Q16.16.
    core::u32 index{0u};   ///< Which entry matched or was proposed.
    bool exact{false};     ///< The normalised names are identical.
    bool proposed{false};  ///< Similar enough to be worth a look, not enough to act on.
};

/**
 * @class AuthorDates
 * @brief A table of author lifespans, queried by name.
 *
 * @warning Held as a sorted vector rather than a hash map, and the reason is the project's: two runs
 * over one corpus must produce the same bytes, and a map's iteration order is a fact about the
 * container. Sorted by normalised name, ties by the order the caller added them.
 */
class AuthorDates {
public:
    /**
     * @brief Adds one author.
     *
     * @param name    As spelled.
     * @param fromDay Earliest day; 0 when unknown.
     * @param toDay   Latest day; 0 when unknown.
     */
    void add(std::string_view name, core::i32 fromDay, core::i32 toDay);

    /**
     * @brief Sorts the table so lookups are deterministic and bisectable.
     *
     * Must be called before @ref find. Separate rather than sorting on every insert, because a
     * catalogue is loaded once and queried thousands of times.
     */
    void finalise();

    /**
     * @brief Looks an author up.
     *
     * @warning An exact match fills the window; a similar one only proposes. The two are different
     * facts and a single "best match" would collapse them -- which is how a resemblance quietly
     * becomes a date.
     *
     * @param name             As the text corpus spells it.
     * @param proposeThreshold Resemblance below which nothing is even proposed, raw Q16.16.
     * @return What was found.
     */
    [[nodiscard]] AuthorDateMatch find(std::string_view name, core::u32 proposeThreshold) const;

    /**
     * @brief How many authors the table holds.
     *
     * @return The count.
     */
    [[nodiscard]] core::u32 size() const noexcept { return static_cast<core::u32>(_entries.size()); }

    /**
     * @brief The entry at an index, as it was added.
     *
     * @param index Zero-based, in the finalised order.
     * @return The entry; an empty one when the index is past the end.
     */
    [[nodiscard]] const AuthorWindow &at(core::u32 index) const;

private:
    struct Entry {
        AuthorWindow window;
        std::string normalised;
    };

    std::vector<Entry> _entries;
    bool _finalised{false};
};

} // namespace lpl::harvest

#endif // LPL_LPL_HARVEST_AUTHORDATES_HPP
