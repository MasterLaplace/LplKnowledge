/**
 * @file Query.cpp
 * @brief Implementation of temporal and structural interrogation without a heap.
 *
 * The matching itself is `constexpr` in the header, because it is a handful of
 * comparisons on a POD and a query runs once per baked row. What lives here is the one
 * part that is neither small nor hot: rendering a question so a person can check the
 * answer against it.
 *
 * Formatting is written out by hand rather than delegated. `snprintf` is a libc call and
 * this module is linked into ring 0, where the contract is no libc at all — the same
 * reason `pack/` writes its own integer formatting.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/knowledge/Query.hpp>

namespace lpl::knowledge {

namespace {

/**
 * @brief Appends a NUL-terminated literal, stopping at the cap.
 *
 * @param out      Destination.
 * @param capacity Room in @p out, NUL included.
 * @param cursor   Current length, advanced in place.
 * @param text     What to append.
 */
void appendText(char *out, core::u32 capacity, core::u32 &cursor, const char *text) noexcept
{
    while (*text != '\0' && cursor + 1u < capacity)
        out[cursor++] = *text++;
}

/**
 * @brief Appends a signed decimal, stopping at the cap.
 *
 * @param out      Destination.
 * @param capacity Room in @p out, NUL included.
 * @param cursor   Current length, advanced in place.
 * @param value    What to append.
 */
void appendNumber(char *out, core::u32 capacity, core::u32 &cursor, core::i64 value) noexcept
{
    if (value < 0)
    {
        appendText(out, capacity, cursor, "-");
        value = -value;
    }

    // Twenty digits covers every 64-bit value, and the widest thing passed here is a year
    // widened from i32 — taken as i64 precisely so negating the most negative i32 is not
    // undefined behaviour.
    char digits[20];
    core::u32 count = 0u;
    do
    {
        digits[count++] = static_cast<char>('0' + static_cast<core::u32>(value % 10));
        value /= 10;
    } while (value != 0 && count < 20u);

    while (count > 0u && cursor + 1u < capacity)
        out[cursor++] = digits[--count];
}

/**
 * @brief Appends one `name=value` term.
 *
 * @param out      Destination.
 * @param capacity Room in @p out, NUL included.
 * @param cursor   Current length, advanced in place.
 * @param name     Term name.
 * @param value    Term value.
 */
void appendTerm(char *out, core::u32 capacity, core::u32 &cursor, const char *name, core::i64 value) noexcept
{
    if (cursor != 0u)
        appendText(out, capacity, cursor, " ");
    appendText(out, capacity, cursor, name);
    appendText(out, capacity, cursor, "=");
    appendNumber(out, capacity, cursor, value);
}

} // namespace

core::u32 describeQuery(const Query &query, char *out, core::u32 capacity) noexcept
{
    if (out == nullptr || capacity == 0u)
        return 0u;

    core::u32 cursor = 0u;

    if (query.subject != kNoIdentifier)
        appendTerm(out, capacity, cursor, "subject", query.subject);
    if (query.predicate != kNoIdentifier)
        appendTerm(out, capacity, cursor, "predicate", query.predicate);
    if (query.object != kNoIdentifier)
        appendTerm(out, capacity, cursor, "object", query.object);
    if (query.source != kNoIdentifier)
        appendTerm(out, capacity, cursor, "source", query.source);
    if (query.year != kAnyYear)
        appendTerm(out, capacity, cursor, "year", query.year);
    if (query.minConfidenceRaw != 0u)
        appendTerm(out, capacity, cursor, "sigma_raw_min", query.minConfidenceRaw);

    // An unconstrained query is a legitimate question — "what is in this image" — so it
    // gets a word rather than an empty line. An empty description would read as a failure
    // to describe.
    if (cursor == 0u)
        appendText(out, capacity, cursor, "everything");

    appendTerm(out, capacity, cursor, "limit", query.limit);

    out[cursor] = '\0';
    return cursor;
}

} // namespace lpl::knowledge
