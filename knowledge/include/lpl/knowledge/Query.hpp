/**
 * @file Query.hpp
 * @brief Temporal and structural interrogation without a heap.
 *
 * 'What was true here, in this year, according to whom' — answered by scanning a
 * sorted section, not by building an index at runtime. The index was baked.
 *
 * A scan is the DESIGN and not a shortcut, and it is worth saying why rather than
 * apologising for it. An interval query wants an interval tree, an interval tree is a
 * pointer structure, and a pointer structure has to be built — which means allocating,
 * in the one reader that must not. The baked order (subject, then window start) makes
 * the two questions asked most often — everything about one subject, everything about
 * one subject in one year — a bounded run over contiguous records, and the cost of
 * every other question is a walk over a section whose length the image declares.
 *
 * This file is also where `graph/Timeline.hpp` went. That scaffold described "ordered
 * validity intervals and their overlaps", which is this, and the fold is recorded in
 * `graph/FOLDED.md`.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_KNOWLEDGE_QUERY_HPP
#    define LPL_LPL_KNOWLEDGE_QUERY_HPP

#    include <lpl/Foundation.hpp>
#    include <lpl/knowledge/Types.hpp>

namespace lpl::knowledge {

/**
 * Year standing for "any year".
 *
 * A sentinel is needed because year zero is a real year and negative years are how a
 * corpus writes antiquity, so no ordinary value is free. The lowest representable year
 * is chosen rather than the highest: a claim that runs to the end of time is a thing a
 * chronicle actually says, and a claim that begins before the lowest representable year
 * is not.
 */
inline constexpr core::i32 kAnyYear = -2147483648;

/**
 * @struct Query
 * @brief What to look for.
 *
 * Every term defaults to "anything", so a default-constructed query means "every claim
 * in the image" and each call narrows it. The alternative — a required subject — reads
 * as safer and is not: it makes "what does this image contain" unaskable, which is the
 * first question anyone has about a file they were handed.
 */
struct Query {
    core::u32 subject{kNoIdentifier};   ///< Constrain the subject, or leave it open.
    core::u32 predicate{kNoIdentifier}; ///< Constrain what is claimed.
    core::u32 object{kNoIdentifier};    ///< Constrain the claimed value.
    core::u32 source{kNoIdentifier};    ///< Constrain who asserts it.
    core::i32 year{kAnyYear};           ///< A year the window must cover.
    core::u32 minConfidenceRaw{0u};     ///< Floor on the raw Q16.16 confidence.

    /**
     * @brief Rows the caller is willing to receive.
     *
     * Part of the query rather than of the call, because a cap is a property of the
     * question: "give me the ten best" and "give me everything" are different questions,
     * and a caller that had to pass the cap separately would eventually pass a different
     * one to the same query in two places.
     */
    core::u32 limit{16u};

    /**
     * @brief Narrows to one subject.
     *
     * @param id The subject identifier.
     * @return This query, for chaining.
     */
    Query &about(core::u32 id) noexcept
    {
        subject = id;
        return *this;
    }

    /**
     * @brief Narrows to claims whose window covers a year.
     *
     * @param value The year.
     * @return This query, for chaining.
     */
    Query &during(core::i32 value) noexcept
    {
        year = value;
        return *this;
    }

    /**
     * @brief Narrows to one asserting source.
     *
     * @param id The source identifier.
     * @return This query, for chaining.
     */
    Query &accordingTo(core::u32 id) noexcept
    {
        source = id;
        return *this;
    }

    /**
     * @brief Narrows to one predicate.
     *
     * @param id The predicate identifier.
     * @return This query, for chaining.
     */
    Query &asserting(core::u32 id) noexcept
    {
        predicate = id;
        return *this;
    }

    /**
     * @brief Discards claims held less firmly than this.
     *
     * @param raw Floor as a raw Q16.16 word.
     * @return This query, for chaining.
     */
    Query &atLeast(core::u32 raw) noexcept
    {
        minConfidenceRaw = raw;
        return *this;
    }

    /**
     * @brief Caps how many rows come back.
     *
     * @param rows The cap.
     * @return This query, for chaining.
     */
    Query &take(core::u32 rows) noexcept
    {
        limit = rows;
        return *this;
    }
};

/**
 * @brief Does one claim satisfy a query?
 *
 * @param query What is being looked for.
 * @param fact  The candidate.
 * @return true when it matches every stated term.
 */
[[nodiscard]] constexpr bool matches(const Query &query, const FactV1 &fact) noexcept
{
    if (query.subject != kNoIdentifier && fact.subject != query.subject)
        return false;
    if (query.predicate != kNoIdentifier && fact.predicate != query.predicate)
        return false;
    if (query.object != kNoIdentifier && fact.object != query.object)
        return false;
    if (query.source != kNoIdentifier && fact.source != query.source)
        return false;
    if (query.minConfidenceRaw != 0u && fact.confidenceRaw < query.minConfidenceRaw)
        return false;
    // Inclusive at both ends: a claim about the year 1204 covers 1204. A half-open window
    // would make an instant — fromYear equal to toYear — cover nothing at all.
    if (query.year != kAnyYear && (query.year < fact.fromYear || query.year > fact.toYear))
        return false;
    return true;
}

/**
 * @brief Writes what a query asked for, in a form a person can read.
 *
 * Exists because an answer without its question is not checkable: `lpl-ask` prints the
 * rows it found, and a reader who cannot see which terms were applied has no way to tell
 * an empty result from a mistyped identifier.
 *
 * Identifiers are rendered as numbers, never resolved through a vocabulary. Resolution
 * needs the image, this file does not have it, and taking one would make the description
 * of a question depend on which image happened to be open.
 *
 * @param query    What was asked.
 * @param out      Receives a NUL-terminated description.
 * @param capacity Room in @p out, NUL included.
 * @return Bytes written, NUL excluded.
 */
[[nodiscard]] core::u32 describeQuery(const Query &query, char *out, core::u32 capacity) noexcept;

} // namespace lpl::knowledge

#endif // LPL_LPL_KNOWLEDGE_QUERY_HPP
