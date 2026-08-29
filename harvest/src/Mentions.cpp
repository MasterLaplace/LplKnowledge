/**
 * @file Mentions.cpp
 * @brief Reading what a primary text names and when it dates it.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/Mentions.hpp>

#include <lpl/corpus/Urn.hpp>
#include <lpl/harvest/Xml.hpp>
#include <lpl/history/Calendar.hpp>

#include <string>

namespace lpl::harvest {

namespace {

/**
 * @brief Reads a signed integer that may carry leading zeroes.
 *
 * @warning Written rather than reused from the gazetteer reader because the two disagree about
 * bounds on purpose: a coordinate is bounded to +/-400 and a year is not. That difference is the
 * whole reason `Pleiades.cpp` keeps its own; sharing one would have made Babylon, attested from
 * -2000, come back undated.
 *
 * @param text   The digits, optionally preceded by a minus.
 * @param cursor Advanced past what was read.
 * @param out    Receives the value.
 * @return false when there were no digits.
 */
[[nodiscard]] bool readSignedRun(std::string_view text, std::size_t &cursor, core::i32 &out) noexcept
{
    bool negative = false;
    if (cursor < text.size() && (text[cursor] == '-' || text[cursor] == '+'))
    {
        negative = text[cursor] == '-';
        ++cursor;
    }
    const std::size_t start = cursor;
    core::i64 value = 0;
    while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9')
    {
        value = value * 10 + (text[cursor] - '0');
        if (value > 3000000)
            return false; // past anything a calendar in this project can address
        ++cursor;
    }
    if (cursor == start)
        return false;
    out = static_cast<core::i32>(negative ? -value : value);
    return true;
}

/**
 * @brief The last day of a month, without a table of month lengths.
 *
 * The day before the first of the next month, so February is right in a leap year without
 * February being mentioned anywhere -- the same reason `lastDayOfYear` is written that way.
 *
 * @param year  The year.
 * @param month 1..12.
 * @return The day number.
 */
[[nodiscard]] core::i32 lastDayOfMonth(core::i32 year, core::u32 month) noexcept
{
    const core::i32 nextYear = month == 12u ? year + 1 : year;
    const core::u32 nextMonth = month == 12u ? 1u : month + 1u;
    return history::dayOfDate(nextYear, nextMonth, 1u) - 1;
}

} // namespace

AuthorityKey splitAuthorityKey(std::string_view key) noexcept
{
    AuthorityKey out;
    const std::size_t comma = key.find(',');
    if (comma == std::string_view::npos || comma == 0u || comma + 1u >= key.size())
        return out;
    out.authority = key.substr(0u, comma);
    out.entry = key.substr(comma + 1u);
    out.present = true;
    return out;
}

core::u32 authorityIdentifier(const AuthorityKey &key) noexcept
{
    if (!key.present)
        return 0u;
    std::string whole;
    whole.reserve(key.authority.size() + key.entry.size() + 1u);
    whole.append(key.authority);
    whole.push_back(',');
    whole.append(key.entry);
    return corpus::nameIdentifier(whole.c_str(), static_cast<core::u32>(whole.size()));
}

bool parseTeiDate(std::string_view text, DateWindow &out) noexcept
{
    out = DateWindow{};
    std::size_t cursor = 0u;
    core::i32 year = 0;
    if (!readSignedRun(text, cursor, year))
        return false;

    // A bare year: the whole of it. This is the common case in the corpus (3841 negative
    // four-digit years alone) and it is exactly what "we only know the year" means.
    if (cursor >= text.size() || text[cursor] != '-')
    {
        out.fromDay = history::firstDayOfYear(year);
        out.toDay = history::lastDayOfYear(year);
        out.known = true;
        return true;
    }

    ++cursor;
    core::i32 month = 0;
    if (!readSignedRun(text, cursor, month) || month < 1 || month > 12)
    {
        // A trailing separator with nothing readable after it is still a year, not a failure:
        // discarding the year because the month is malformed would lose what the editor did say.
        out.fromDay = history::firstDayOfYear(year);
        out.toDay = history::lastDayOfYear(year);
        out.known = true;
        return true;
    }

    if (cursor >= text.size() || text[cursor] != '-')
    {
        out.fromDay = history::dayOfDate(year, static_cast<core::u32>(month), 1u);
        out.toDay = lastDayOfMonth(year, static_cast<core::u32>(month));
        out.known = true;
        return true;
    }

    ++cursor;
    core::i32 day = 0;
    if (!readSignedRun(text, cursor, day) || day < 1 || day > 31)
    {
        out.fromDay = history::dayOfDate(year, static_cast<core::u32>(month), 1u);
        out.toDay = lastDayOfMonth(year, static_cast<core::u32>(month));
        out.known = true;
        return true;
    }

    out.fromDay = history::dayOfDate(year, static_cast<core::u32>(month), static_cast<core::u32>(day));
    out.toDay = out.fromDay;
    out.known = true;
    return true;
}

bool dateWindowOf(std::string_view tag, DateWindow &out) noexcept
{
    out = DateWindow{};

    std::string value;
    // @warning **A date in a calendar this reader does not know is REFUSED, never parsed.** TEI's
    // default calendar is Gregorian, and an editor who means something else says so -- with
    // `@datingMethod`, `@calendar`, or by putting the original reckoning in `@when-custom` and
    // leaving `@when` for the normalised form. Measured across the Perseus corpus: zero
    // `datingMethod`, zero `calendar`, and 11 `when-custom`. So the marked-up dates really are
    // already normalised by the editor -- which is the editor's job and not a guess this reader
    // is entitled to make -- and the exceptions are few, real, and identifiable.
    //
    // Reading a Julian date as a proleptic Gregorian one shifts it by ten days in 1582 and by
    // more the further back one goes: a silent, plausible, wrong answer, which is the worst kind.
    //
    // @warning `when-custom` is deliberately NOT a refusal, and a first version made it one. It holds
    // the reckoning as the source states it -- "Ol. 75.1" -- beside whatever the editor
    // normalised; refusing on its presence threw away a perfectly good `from`/`to` from an editor
    // who had done the work properly. It needs no rule at all: a custom reckoning is not a date
    // this parser can read, so it is simply never read, and the normalised attributes beside it
    // are. The probe that found this passed for the wrong reason -- there was nothing to parse in
    // the fixture either way.
    if (xmlAttribute(tag, "datingMethod", value) || xmlAttribute(tag, "calendar", value))
        return false;
    // `when` first: it is the most precise thing the element can say, and an element carrying
    // both it and a range is stating a point inside that range.
    if (xmlAttribute(tag, "when", value) && parseTeiDate(value, out))
        return true;

    DateWindow lower;
    DateWindow upper;
    std::string other;
    const bool hasFrom = xmlAttribute(tag, "from", value) && parseTeiDate(value, lower);
    const bool hasTo = xmlAttribute(tag, "to", other) && parseTeiDate(other, upper);
    if (hasFrom || hasTo)
    {
        // @warning The OUTER edges of the two, so the window covers everything the element claims.
        // Taking the inner edges would narrow a span into a point whenever an editor dated both
        // ends -- turning the most carefully dated entries into the most precise-looking ones,
        // which is the opposite of what they say.
        out.fromDay = hasFrom ? lower.fromDay : upper.fromDay;
        out.toDay = hasTo ? upper.toDay : lower.toDay;
        out.known = true;
        return true;
    }

    const bool hasNotBefore = xmlAttribute(tag, "notBefore", value) && parseTeiDate(value, lower);
    const bool hasNotAfter = xmlAttribute(tag, "notAfter", other) && parseTeiDate(other, upper);
    if (hasNotBefore || hasNotAfter)
    {
        out.fromDay = hasNotBefore ? lower.fromDay : upper.fromDay;
        out.toDay = hasNotAfter ? upper.toDay : lower.toDay;
        out.known = true;
        return true;
    }

    return false;
}

} // namespace lpl::harvest
