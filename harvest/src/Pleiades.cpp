/**
 * @file Pleiades.cpp
 * @brief Implementation of the ancient-places reader.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/Pleiades.hpp>

#include <lpl/corpus/Urn.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace lpl::harvest {

namespace {

/**
 * @brief Reads a whole file.
 *
 * @param path Where.
 * @param out  Receives the bytes.
 * @return false when the file could not be opened.
 */
[[nodiscard]] bool readFile(const std::string &path, std::string &out)
{
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return false;
    char chunk[65536];
    std::size_t read = 0u;
    while ((read = std::fread(chunk, 1u, sizeof(chunk), file)) > 0u)
        out.append(chunk, read);
    return std::fclose(file) == 0;
}

/**
 * @brief Whether a comma-separated list contains a term.
 *
 * @param list The list, as the dump writes it.
 * @param term The term.
 * @return true when present.
 */
[[nodiscard]] bool listHas(std::string_view list, std::string_view term) noexcept
{
    std::size_t at = 0u;
    while (at < list.size())
    {
        std::size_t stop = list.find(',', at);
        if (stop == std::string_view::npos)
            stop = list.size();
        std::string_view item = list.substr(at, stop - at);
        while (!item.empty() && item.front() == ' ')
            item.remove_prefix(1u);
        while (!item.empty() && item.back() == ' ')
            item.remove_suffix(1u);
        if (item == term)
            return true;
        at = stop + 1u;
    }
    return false;
}

/**
 * @brief Maps the repository's feature vocabulary onto the wire's kind bits.
 *
 * @warning A closed mapping, and anything outside it contributes NO bit rather than a guessed one.
 * Pleiades names well over a hundred feature types; inventing a bit for each would put a
 * vocabulary decision in a reader, and claiming "settlement" for an unrecognised word would put
 * people in places nobody said were inhabited.
 *
 * @param types The comma-separated feature types.
 * @return The kind bits.
 */
[[nodiscard]] core::u32 kindsOf(std::string_view types) noexcept
{
    core::u32 kinds = 0u;
    if (listHas(types, "settlement") || listHas(types, "villa") || listHas(types, "vicus"))
        kinds |= knowledge::kPlaceKindSettlement;
    if (listHas(types, "urban"))
        kinds |= knowledge::kPlaceKindUrban | knowledge::kPlaceKindSettlement;
    if (listHas(types, "river") || listHas(types, "lake") || listHas(types, "spring") ||
        listHas(types, "well") || listHas(types, "bay") || listHas(types, "canal"))
        kinds |= knowledge::kPlaceKindWater;
    if (listHas(types, "fort") || listHas(types, "fortification") || listHas(types, "wall") ||
        listHas(types, "tower") || listHas(types, "military-installation"))
        kinds |= knowledge::kPlaceKindFortification;
    if (listHas(types, "temple") || listHas(types, "sanctuary") || listHas(types, "church") ||
        listHas(types, "altar") || listHas(types, "shrine"))
        kinds |= knowledge::kPlaceKindSacred;
    if (listHas(types, "road") || listHas(types, "station") || listHas(types, "pass") ||
        listHas(types, "bridge") || listHas(types, "port"))
        kinds |= knowledge::kPlaceKindRoute;
    if (listHas(types, "mountain") || listHas(types, "island") || listHas(types, "cape") ||
        listHas(types, "plain") || listHas(types, "valley") || listHas(types, "hill"))
        kinds |= knowledge::kPlaceKindLandform;
    return kinds;
}

/**
 * @brief Reads a signed decimal integer, with no range of its own.
 *
 * Separate from @ref degreesToRaw because the bound is the difference: a coordinate past 400 is
 * malformed, a YEAR past 400 is most of antiquity. A fractional part is accepted and truncated —
 * the dump writes some dates as "-484.0".
 *
 * @param text The number.
 * @param out  Receives it.
 * @return false when the text holds no integer.
 */
[[nodiscard]] bool signedInteger(std::string_view text, core::i32 &out) noexcept
{
    out = 0;
    std::size_t i = 0u;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
        ++i;
    const bool negative = i < text.size() && text[i] == '-';
    if (negative || (i < text.size() && text[i] == '+'))
        ++i;
    if (i >= text.size() || text[i] < '0' || text[i] > '9')
        return false;
    core::i64 value = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9')
    {
        value = value * 10 + static_cast<core::i64>(text[i] - '0');
        if (value > 2000000000) // refuse rather than wrap
            return false;
        ++i;
    }
    out = static_cast<core::i32>(negative ? -value : value);
    return true;
}

} // namespace

bool nextCsvRecord(std::string_view body, std::size_t &cursor, std::string &out)
{
    out.clear();
    if (cursor >= body.size())
        return false;

    bool inQuotes = false;
    const std::size_t begin = cursor;
    while (cursor < body.size())
    {
        const char c = body[cursor];
        if (c == '"')
        {
            // A doubled quote INSIDE a quoted field is an escaped quote, not a close. Getting
            // this wrong flips the quoting state and every field after it lands one column off.
            if (inQuotes && cursor + 1u < body.size() && body[cursor + 1u] == '"')
            {
                cursor += 2u;
                continue;
            }
            inQuotes = !inQuotes;
            ++cursor;
            continue;
        }
        // @warning A record is not a line. A quoted field may hold newlines, and Pleiades descriptions
        // do — so a reader that stops at every '\n' cuts records in half.
        if (c == '\n' && !inQuotes)
        {
            out.assign(body.substr(begin, cursor - begin));
            ++cursor;
            if (!out.empty() && out.back() == '\r')
                out.pop_back();
            return true;
        }
        ++cursor;
    }
    out.assign(body.substr(begin, cursor - begin));
    if (!out.empty() && out.back() == '\r')
        out.pop_back();
    return !out.empty();
}

void splitCsvRecord(std::string_view record, std::vector<std::string> &out)
{
    out.clear();
    std::string field;
    bool inQuotes = false;
    for (std::size_t i = 0u; i < record.size(); ++i)
    {
        const char c = record[i];
        if (inQuotes)
        {
            if (c != '"')
            {
                field.push_back(c);
                continue;
            }
            if (i + 1u < record.size() && record[i + 1u] == '"')
            {
                field.push_back('"');
                ++i;
                continue;
            }
            inQuotes = false;
            continue;
        }
        if (c == '"')
        {
            inQuotes = true;
            continue;
        }
        if (c == ',')
        {
            out.push_back(field);
            field.clear();
            continue;
        }
        field.push_back(c);
    }
    // @warning The last field is pushed unconditionally: a record ending in a comma has a trailing
    // EMPTY field, and dropping it shortens the row so every index-based read after it is wrong.
    out.push_back(field);
}

bool degreesToRaw(std::string_view text, core::i32 &out) noexcept
{
    out = 0;
    std::size_t i = 0u;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
        ++i;
    const bool negative = i < text.size() && text[i] == '-';
    if (negative || (i < text.size() && text[i] == '+'))
        ++i;
    if (i >= text.size() || ((text[i] < '0' || text[i] > '9') && text[i] != '.'))
        return false;

    core::i64 whole = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9')
    {
        whole = whole * 10 + static_cast<core::i64>(text[i] - '0');
        if (whole > 400) // far past any longitude; refuse rather than wrap
            return false;
        ++i;
    }

    // The fraction, scaled by 65536 with integer arithmetic only. @warning Never through a double: the
    // same decimal string has to land on the same word on every target, and a coordinate that
    // rounds differently is a body that walks to a different place.
    core::i64 fraction = 0;
    if (i < text.size() && text[i] == '.')
    {
        ++i;
        core::i64 numerator = 0;
        core::i64 denominator = 1;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9' && denominator <= 100000000)
        {
            numerator = numerator * 10 + static_cast<core::i64>(text[i] - '0');
            denominator *= 10;
            ++i;
        }
        // Remaining digits are past the format's resolution (1/65536 ≈ 1.7 m) and are dropped
        // rather than rounded, so the result depends only on the digits that can matter.
        while (i < text.size() && text[i] >= '0' && text[i] <= '9')
            ++i;
        fraction = (numerator * 65536 + denominator / 2) / denominator;
    }

    const core::i64 raw = whole * 65536 + fraction;
    out = static_cast<core::i32>(negative ? -raw : raw);
    return true;
}

bool ingestPleiades(const std::string &path, Baker &baker, GazetteerIngestReport &outReport)
{
    // @warning Pleiades is CC-BY 3.0, and this reader baked its 42 400 places with NO credit at all
    // until the rule was generalised: an image carrying it was one nobody could lawfully forward.
    // Found by asking what ELSE the relief attribution applied to, rather than by re-reading.
    baker.addAttribution("Pleiades gazetteer of the ancient world, CC-BY 3.0 "
                         "(https://pleiades.stoa.org/credits) - see https://creativecommons.org/licenses/by/3.0/");

    outReport = GazetteerIngestReport{};

    std::string body;
    if (!readFile(path, body))
        return false;

    std::size_t cursor = 0u;
    std::string record;
    if (!nextCsvRecord(body, cursor, record))
        return false;

    // Columns are found BY NAME, never by position. The dump's column order is the repository's
    // business and has changed before; a reader pinned to indices reads the description as a
    // latitude the day a column is inserted, and nothing about the output looks wrong.
    std::vector<std::string> header;
    splitCsvRecord(record, header);
    std::map<std::string, std::size_t> column;
    for (std::size_t i = 0u; i < header.size(); ++i)
        column.emplace(header[i], i);

    const char *needed[] = {"id", "title", "reprLat", "reprLong", "minDate", "maxDate",
                            "featureTypes", "locationPrecision"};
    // @warning NOT in `needed`: a dump without it is still a usable gazetteer, just one whose places
    // are not known to be connected. Requiring it would refuse a whole corpus over a column that
    // 27 000 of 42 400 rows leave empty anyway.
    const auto linkColumn = column.find("connectsWith");
    for (const char *name : needed)
        if (column.find(name) == column.end())
            return false; // not a Pleiades dump; refused rather than half-read

    const std::size_t idAt = column["id"];
    const std::size_t titleAt = column["title"];
    const std::size_t latAt = column["reprLat"];
    const std::size_t lonAt = column["reprLong"];
    const std::size_t minAt = column["minDate"];
    const std::size_t maxAt = column["maxDate"];
    const std::size_t typesAt = column["featureTypes"];
    const std::size_t precisionAt = column["locationPrecision"];

    std::vector<std::string> fields;
    while (nextCsvRecord(body, cursor, record))
    {
        if (record.empty())
            continue;
        splitCsvRecord(record, fields);
        if (fields.size() <= precisionAt)
        {
            ++outReport.malformed;
            continue;
        }

        const core::u32 place = static_cast<core::u32>(std::strtoul(fields[idAt].c_str(), nullptr, 10));
        if (place == 0u)
        {
            // Zero is the format's "none"; a row whose identifier reads as zero has no key, and
            // a gazetteer entry without a key cannot be pointed at by anything.
            ++outReport.malformed;
            continue;
        }

        knowledge::GazetteerEntryV1 entry{};
        entry.place = place;
        entry.title = fields[titleAt].empty() ? knowledge::kNoIdentifier
                                              : baker.addText(fields[titleAt]) + 1u;

        core::i32 lat = 0;
        core::i32 lon = 0;
        if (degreesToRaw(fields[latAt], lat) && degreesToRaw(fields[lonAt], lon))
        {
            entry.latRaw = lat;
            entry.lonRaw = lon;
            entry.flags |= knowledge::kGazetteerFlagLocated;
            ++outReport.located;
        }
        else
        {
            ++outReport.unlocated;
        }
        if (fields[precisionAt] == "precise")
            entry.flags |= knowledge::kGazetteerFlagPrecise;

        core::i32 minYear = 0;
        core::i32 maxYear = 0;
        // @warning NOT `degreesToRaw`, and a first version made exactly that mistake. That parser is
        // bounded at 400 because no coordinate exceeds it — so Babylon, attested from -2000,
        // would have come back UNDATED, silently and plausibly. And `readYear` is no better: it
        // wants four digits and refuses "-484", which is the half of history that matters here.
        if (signedInteger(fields[minAt], minYear) && signedInteger(fields[maxAt], maxYear))
        {
            entry.minYear = minYear;
            entry.maxYear = maxYear;
            entry.flags |= knowledge::kGazetteerFlagDated;
            ++outReport.dated;
        }

        // The attested links, in both directions. @warning Written even when the neighbour is not in
        // this dump: a link to a place the corpus does not describe is still a fact about this
        // one, and a resolver simply fails to resolve it — which is different from pretending
        // the road was never there.
        if (linkColumn != column.end() && linkColumn->second < fields.size())
        {
            const std::string &list = fields[linkColumn->second];
            std::size_t at = 0u;
            while (at < list.size())
            {
                std::size_t stop = list.find(',', at);
                if (stop == std::string::npos)
                    stop = list.size();
                const core::u32 neighbour =
                    static_cast<core::u32>(std::strtoul(list.c_str() + at, nullptr, 10));
                if (neighbour != 0u && neighbour != place)
                {
                    baker.addPlaceLink(place, neighbour);
                    ++outReport.links;
                }
                at = stop + 1u;
            }
        }

        entry.kinds = kindsOf(fields[typesAt]);
        if ((entry.kinds & knowledge::kPlaceKindSettlement) != 0u)
            ++outReport.settlements;

        baker.addGazetteerEntry(entry);
        ++outReport.places;
    }

    return outReport.places != 0u;
}

} // namespace lpl::harvest
