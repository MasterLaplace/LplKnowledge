/**
 * @file Predicates.hpp
 * @brief Which predicates carry words rather than identifiers.
 *
 * A `FactV1::object` is a `u32`, and that one word means two different things depending on
 * the predicate: for most of them it is an identifier the vocabulary can name, and for a
 * handful it is an INDEX into the image's `Texts` section. Nothing in the wire record says
 * which, because the wire record is the same 32 bits either way.
 *
 * @warning The consequence, measured rather than imagined: `lpl-ask` printed `#1001` where a
 * passage of Caesar should have been, and `#7` where a research run's topic should have been.
 * An index that cannot show what it indexed is a catalogue, not a library.
 *
 * So the answer lives HERE, once, instead of as three lists in three tools. The cost of that
 * choice is stated plainly: a reader that mints a new text-valued predicate and does not add
 * it below will have every tool print `#N` for it, and nothing will fail — so the list is
 * ordered by the header that declares each entry, to make the omission visible when reading.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_PREDICATES_HPP
#    define LPL_LPL_HARVEST_PREDICATES_HPP

#    include <lpl/Foundation.hpp>
#    include <lpl/harvest/Markdown.hpp>
#    include <lpl/harvest/ResearchReport.hpp>
#    include <lpl/harvest/Tei.hpp>

namespace lpl::harvest {

/**
 * @brief Is this predicate's object a line of the Texts section?
 *
 * @param predicate The predicate identifier.
 * @return true when the object should be rendered as words rather than resolved as a name.
 */
[[nodiscard]] constexpr bool objectIsTextLine(core::u32 predicate) noexcept
{
    switch (predicate)
    {
    // Markdown.hpp
    case kPredicateDefinitionText:
    // ResearchReport.hpp
    case kPredicateFindingText:
    case kPredicateSourceUrl:
    case kPredicateResearches:
    // Tei.hpp
    case kPredicateWorkTitle:
    case kPredicatePassageText:
    case kPredicateCitationScheme:
        return true;
    default:
        return false;
    }
}

} // namespace lpl::harvest

#endif // LPL_LPL_HARVEST_PREDICATES_HPP
