/**
 * @file Locus.cpp
 * @brief Implementation of a precise position inside a work.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/corpus/Locus.hpp>

namespace lpl::corpus {

namespace {

/// Deepest CTS passage this scheme can express, since a Locus has three ordinals.
constexpr core::u32 kMaxDepth = 3u;

/**
 * @brief Appends an unsigned decimal, stopping at the cap.
 *
 * @param out      Destination.
 * @param capacity Room in @p out, NUL included.
 * @param cursor   Current length, advanced in place.
 * @param value    What to append.
 */
void appendNumber(char *out, core::u32 capacity, core::u32 &cursor, core::u32 value) noexcept
{
    char digits[10];
    core::u32 count = 0u;
    do
    {
        digits[count++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < 10u);

    while (count > 0u && cursor + 1u < capacity)
        out[cursor++] = digits[--count];
}

} // namespace

bool parsePassage(const char *text, core::u32 bytes, Locus &out) noexcept
{
    out = Locus{};
    if (text == nullptr || bytes == 0u)
        return false;

    core::u32 levels[kMaxDepth] = {0u, 0u, 0u};
    core::u32 depth = 0u;
    core::u32 cursor = 0u;

    while (cursor < bytes)
    {
        if (depth == kMaxDepth)
            return false; // deeper than three levels: refused, never truncated

        core::u32 value = 0u;
        const core::u32 digitStart = cursor;
        while (cursor < bytes && text[cursor] >= '0' && text[cursor] <= '9')
        {
            const core::u32 digit = static_cast<core::u32>(text[cursor] - '0');
            // Bounded before it overflows rather than after: a wrapped ordinal names a
            // real position in the text, so it would be accepted as a plausible citation.
            if (value > (0xFFFFFFFFu - digit) / 10u)
                return false;
            value = value * 10u + digit;
            ++cursor;
        }
        if (cursor == digitStart)
            return false; // an empty level, e.g. "1..3"

        levels[depth++] = value;

        if (cursor == bytes)
            break;
        if (text[cursor] != '.')
            return false; // a citation range or a non-numeric reference; not this scheme
        ++cursor;
        if (cursor == bytes)
            return false; // a trailing separator, e.g. "1."
    }

    // Filled from the FINEST level upward, which is the mapping Locus.hpp states: a
    // one-level citation names a line, not a book.
    switch (depth)
    {
    case 1u: out.line = levels[0]; break;
    case 2u:
        out.section = levels[0];
        out.line = levels[1];
        break;
    case 3u:
        out.part = levels[0];
        out.section = levels[1];
        out.line = levels[2];
        break;
    default: return false;
    }
    return true;
}

core::u32 renderLocus(const Locus &locus, bool plainText, char *out, core::u32 capacity) noexcept
{
    if (out == nullptr || capacity == 0u)
        return 0u;

    core::u32 cursor = 0u;

    if (plainText)
    {
        appendNumber(out, capacity, cursor, locus.line);
        out[cursor] = '\0';
        return cursor;
    }

    // Only the levels the locus actually carries are printed, so a passage read as "1.5"
    // renders back as "1.5" and not as "0.1.5". A round trip that changed the depth would
    // change what the citation means.
    if (locus.part != 0u)
    {
        appendNumber(out, capacity, cursor, locus.part);
        if (cursor + 1u < capacity)
            out[cursor++] = '.';
    }
    if (locus.part != 0u || locus.section != 0u)
    {
        appendNumber(out, capacity, cursor, locus.section);
        if (cursor + 1u < capacity)
            out[cursor++] = '.';
    }
    appendNumber(out, capacity, cursor, locus.line);

    out[cursor] = '\0';
    return cursor;
}

} // namespace lpl::corpus
