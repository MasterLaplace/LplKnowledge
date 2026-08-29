/**
 * @file Pleiades.hpp
 * @brief Reading a gazetteer of the ancient world.
 *
 * The piece the bridge between a corpus and a world was missing, and it turned out to be already
 * published. Pleiades is a community gazetteer of ancient places, CC-BY 3.0, and it carries
 * exactly what a historical simulation needs and a procedural world cannot invent: a stable
 * identifier, coordinates, and **the years a place is attested between**.
 *
 * Measured on the full dump rather than assumed: 42 400 places, 34 797 with coordinates, 39 585
 * with a temporal window, **12 664 settlements both located and dated**, in 6.8 MB.
 *
 * @warning **A name is not an identity, and this corpus proves it on contact.** Ten distinct places are
 * called "Alexandria", from Egypt to Afghanistan, and several are called "Athenae" — one of them
 * unlocated. Anything keyed on the title puts the Library of Alexandria in Kabul. The reader
 * therefore keys on the repository's own numeric identifier, which is the rule
 * @ref EntityResolution already states: a hard key merges, a score only proposes.
 *
 * @warning **A real CSV reader, not a field split.** The dump quotes fields that contain commas, embedded
 * newlines and whole JSON documents — the `extent` column is GeoJSON. Splitting on commas reads
 * the third element of a coordinate array as a title.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_PLEIADES_HPP
#    define LPL_LPL_HARVEST_PLEIADES_HPP

#    include <lpl/Foundation.hpp>
#    include <lpl/harvest/Baker.hpp>

#    include <string>
#    include <string_view>
#    include <vector>

namespace lpl::harvest {

/**
 * @struct GazetteerIngestReport
 * @brief What a gazetteer harvest read, and what it refused.
 */
struct GazetteerIngestReport {
    core::u32 places{0u};      ///< Rows written.
    core::u32 located{0u};     ///< With coordinates.
    core::u32 dated{0u};       ///< With a temporal window.
    core::u32 settlements{0u}; ///< Places people lived in.
    core::u32 unlocated{0u};   ///< Known from texts, never found. Kept, never invented.
    core::u32 malformed{0u};   ///< Rows the reader could not use at all.
    core::u32 links{0u};       ///< Attested connections read, as the source declares them.
};

/**
 * @brief Splits one CSV record into fields.
 *
 * Exposed because it is the part with the bug in it, and the bug is silent: a quoted field may
 * contain commas, doubled quotes and newlines, so a reader that splits on commas returns a
 * different NUMBER of fields for some rows than for others — and every column after the first
 * quoted one is then read from the wrong place, on exactly the rows whose data is richest.
 *
 * @param record One logical record, quotes included.
 * @param out    Receives the fields, unquoted and unescaped.
 */
void splitCsvRecord(std::string_view record, std::vector<std::string> &out);

/**
 * @brief Reads one logical CSV record from a stream position.
 *
 * A record is not a line: a quoted field may hold newlines, and Pleiades descriptions do.
 *
 * @param body   The whole document.
 * @param cursor Where to start; advanced past the record.
 * @param out    Receives the record, without its terminator.
 * @return false at end of input.
 */
[[nodiscard]] bool nextCsvRecord(std::string_view body, std::size_t &cursor, std::string &out);

/**
 * @brief Converts decimal degrees to a raw Q16.16 word.
 *
 * @warning Parsed here rather than through `strtod` and a cast, because the cast is where a coordinate
 * stops being reproducible: the same decimal string must land on the same integer on every
 * target, and a float that rounds differently is a body that walks to a different place. The
 * digits are accumulated as integers and the fraction is scaled by 65536 exactly.
 *
 * @param text The decimal number, e.g. "-27.4221505".
 * @param out  Receives the raw word.
 * @return false when the text holds no number.
 */
[[nodiscard]] bool degreesToRaw(std::string_view text, core::i32 &out) noexcept;

/**
 * @brief Ingests a Pleiades CSV dump.
 *
 * @warning Places with no coordinates are WRITTEN, without them, and counted. 6 502 of them are marked
 * `unlocated` — known from texts and never found on the ground — and giving those a position
 * would turn "nobody knows where Cimmeria was" into a map pin. The flag says which is which.
 *
 * @param path      The dump.
 * @param baker     Where places go.
 * @param outReport Receives the tally.
 * @return false when the file could not be read, or is not a Pleiades dump.
 */
[[nodiscard]] bool ingestPleiades(const std::string &path, Baker &baker,
                                  GazetteerIngestReport &outReport);

} // namespace lpl::harvest

#endif // LPL_LPL_HARVEST_PLEIADES_HPP
