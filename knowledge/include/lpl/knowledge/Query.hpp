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
#    include <lpl/history/Calendar.hpp>
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
 * Language sentinel meaning "do not constrain".
 *
 * @warning **Not `kNoIdentifier`**, and that distinction is the whole reason this constant exists.
 * Zero is a MEANINGFUL value of `corpus::LanguageTag` — it is `Unknown` — so a query field
 * defaulting to zero made "leave the language open" and "find the ones whose language nobody
 * recorded" the same request, and the second was unaskable. Measured: `--language 0` over
 * Project Gutenberg returned **79 179 of 79 179** rows while 11 826 of them are in a language
 * this tag set cannot name, and there was no way to ask for exactly those.
 *
 * The same shape as @ref kAnyYear beside it, and for the same reason: a field whose entire range
 * is meaningful needs a sentinel from OUTSIDE that range, never a value borrowed from inside it.
 */
inline constexpr core::u32 kAnyLanguage = 0xFFFFFFFFu;

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
    /**
     * First day the claim's window must reach; @ref kAnyYear for no bound.
     *
     * @warning **A pair, because asking about a year is asking about 365 days.** This was a single
     * `year` compared against `fact.fromDay` -- a year number tested against a day number, so
     * `during(1204)` asked for day 1204 while the fact covered day 439 000, and every query
     * returned nothing. The rename from years to days went through the FIELD and stopped at the
     * caller, which is exactly where a unit change hides.
     */
    core::i32 dayFrom{kAnyYear};

    /// Last day it must reach; @ref kAnyYear for no bound.
    core::i32 dayTo{kAnyYear};
    core::u32 minConfidenceRaw{0u};     ///< Floor on the raw Q16.16 confidence.
    /**
     * Constrain WHERE in a document, as a one-based Loci index, or leave it open.
     *
     * A passage is a position rather than a thing, so this is how a passage is asked for.
     * @warning The alternative — minting an identifier per passage and constraining @c subject —
     * was tried and does not survive contact with a corpus: `nameIdentifier` is 32 bits, and
     * Perseus alone carries some 700 000 passages, so two of them collided on one identifier
     * within seconds. `FactV1` already carried both a subject and a locus; the second was the
     * one that meant "which passage" all along.
     */
    core::u32 locus{kNoIdentifier};

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
    /**
     * @brief Narrows to one position inside a document.
     *
     * @param index One-based Loci index, as a fact carries it.
     * @return This query, for chaining.
     */
    Query &at(core::u32 index) noexcept
    {
        locus = index;
        return *this;
    }

    Query &about(core::u32 id) noexcept
    {
        subject = id;
        return *this;
    }

    /**
     * @brief Narrows to claims whose window OVERLAPS a year.
     *
     * @warning Overlap, not containment, and the whole year rather than a day of it. A claim dated
     * to a single day inside 1204 is a claim about 1204; so is one spanning 1200 to 1250. Asking
     * for containment would return the second and drop the first, which is the more precise of
     * the two.
     *
     * @param value The year.
     * @return This query, for chaining.
     */
    Query &during(core::i32 value) noexcept
    {
        dayFrom = history::firstDayOfYear(value);
        dayTo = history::lastDayOfYear(value);
        return *this;
    }

    /**
     * @brief Narrows to claims whose window covers one exact day.
     *
     * The half a year-shaped query cannot express, and the reason the unit changed: a source that
     * knew a date to the day deserves to be findable by it.
     *
     * @param day The day, in the epoch `history::Calendar` defines.
     * @return This query, for chaining.
     */
    Query &onDay(core::i32 day) noexcept
    {
        dayFrom = day;
        dayTo = day;
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
    if (query.locus != kNoIdentifier && fact.locus != query.locus)
        return false;
    // Inclusive at both ends: a claim about the year 1204 covers 1204. A half-open window
    // would make an instant — fromDay equal to toDay — cover nothing at all.
    // Interval overlap in both directions: the query's window and the claim's must share a day.
    if (query.dayFrom != kAnyYear && fact.toDay < query.dayFrom)
        return false;
    if (query.dayTo != kAnyYear && query.dayTo < fact.fromDay)
        return false;
    return true;
}

/**
 * @struct CatalogueQuery
 * @brief What to look for among holdings.
 *
 * Separate from @ref Query rather than folded into it, and the reason is what the two ask
 * about. A `Query` asks what is CLAIMED — subject, predicate, source, confidence. A catalogue
 * claims nothing; it records that a holder says it has a thing. Sharing one struct would put
 * a confidence floor on a row that has no confidence, and would invite a caller to filter
 * holdings by a predicate they do not have.
 *
 * Every term is an integer comparison, so this belongs beside the reader rather than in the
 * hosted half: a constrained target that can open an image can also answer "what does this
 * holder have from before 1800" without a heap.
 */
struct CatalogueQuery {
    core::u32 holder{kNoIdentifier};   ///< Constrain the repository, or leave it open.
    /**
     * Constrain the language tag, or @ref kAnyLanguage to leave it open.
     *
     * @warning Zero here means `LanguageTag::Unknown` and asks for rows whose language the source did
     * not state — a real question, and one that used to be impossible to put.
     */
    core::u32 language{kAnyLanguage};
    core::i32 fromDay{kAnyYear};      ///< Earliest year, inclusive.
    core::i32 toDay{kAnyYear};        ///< Latest year, inclusive.
    /**
     * Bits every match must carry, e.g. `kCatalogueFlagPublicDomain`.
     *
     * @warning A row with NO year is not a row from year zero, and a filter on years must not quietly
     * admit it. @ref matchesCatalogue excludes unknown years whenever a bound is stated, so
     * "published before 1800" never silently means "or undated".
     */
    core::u32 requiredFlags{0u};
    core::u32 forbiddenFlags{0u}; ///< Bits no match may carry.
    core::u32 limit{16u};         ///< Rows the caller is willing to receive.
};

/**
 * @brief Does one holding satisfy a catalogue query?
 *
 * @param query What is being looked for.
 * @param entry The candidate.
 * @return true when it matches every stated term.
 */
[[nodiscard]] constexpr bool matchesCatalogue(const CatalogueQuery &query,
                                              const CatalogueEntryV1 &entry) noexcept
{
    if (query.holder != kNoIdentifier && entry.holder != query.holder)
        return false;
    if (query.language != kAnyLanguage && entry.language != query.language)
        return false;
    if ((entry.flags & query.requiredFlags) != query.requiredFlags)
        return false;
    if ((entry.flags & query.forbiddenFlags) != 0u)
        return false;
    // @warning Year zero means UNKNOWN in this record, so a stated bound excludes it rather than
    // treating it as the year 0. Measured need: 19 605 849 HathiTrust rows all carry a year,
    // but Gutenberg rows routinely do not, and "before 1800" must not come to include them.
    if (query.fromDay != kAnyYear || query.toDay != kAnyYear)
    {
        if (entry.year == 0)
            return false;
        // @warning OVERLAP, not containment. A holding carries a window now, so "published before
        // 1900" must admit a work whose window starts in 1850 and ends in 1920 -- it may well be
        // from before 1900, and refusing it would hide exactly the works whose dating is
        // uncertain, which are the old ones.
        const core::i32 last = entry.yearTo != 0 ? entry.yearTo : entry.year;
        if (query.fromDay != kAnyYear && last < query.fromDay)
            return false;
        if (query.toDay != kAnyYear && entry.year > query.toDay)  // the window starts too late
            return false;
    }
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
