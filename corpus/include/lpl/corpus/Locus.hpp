/**
 * @file Locus.hpp
 * @brief A precise position inside a work.
 *
 * Book, chapter, line — the granularity historians actually cite, which is finer
 * than a document and coarser than a byte offset.
 *
 * Coarser than a byte offset on purpose: an offset is invalidated by re-encoding a file,
 * normalising its line endings or fixing one typo upstream, and a citation that breaks
 * when a file is touched is a citation nobody can check a year later. Three ordinals
 * survive all of that.
 *
 * ⚠ A CTS passage has a depth that VARIES BY WORK — a poem cites one level, a history
 * three — so the mapping from depth to these three fields is stated once, here, and
 * nowhere else. Getting it stated in two places would mean the same passage naming two
 * different positions depending on which parser saw it.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_CORPUS_LOCUS_HPP
#    define LPL_LPL_CORPUS_LOCUS_HPP

#    include <lpl/Foundation.hpp>

namespace lpl::corpus {

/**
 * @struct Locus
 * @brief A position, as three ordinals.
 */
struct Locus {
    core::u32 part{0u};    ///< Book, or 0 when the work has no such level.
    core::u32 section{0u}; ///< Chapter, or the ordinal of the enclosing heading.
    core::u32 line{0u};    ///< Line within the section.
};

/**
 * @brief Parses a CTS passage reference into a locus.
 *
 * The depth-to-field mapping, and it is a choice worth seeing: the ordinals are filled
 * from the FINEST level upward, so a one-level passage names a line, a two-level passage
 * names a section and a line, and a three-level passage names all three. Filling from the
 * coarsest end instead would make "5" mean book five, which is not what a one-level
 * citation means in any corpus this project can read.
 *
 * Deeper than three levels is REFUSED rather than truncated: silently dropping the finest
 * level of a four-level citation would move it to a different position in the text.
 *
 * @param text  First byte of the passage part, e.g. "1.5" or "3.12.4".
 * @param bytes Its length.
 * @param out   Receives the locus.
 * @return false when the reference is empty, malformed, or deeper than three levels.
 */
[[nodiscard]] bool parsePassage(const char *text, core::u32 bytes, Locus &out) noexcept;

/**
 * @brief The locus of one line of a plain-text document.
 *
 * The other half of the addressing scheme `kDocumentFlagPlainText` names: a working
 * document has no books, so @c part stays zero, @c section is the ordinal of the heading
 * the line sits under, and @c line is the line number in the file.
 *
 * @param heading Ordinal of the enclosing heading, or 0 above the first one.
 * @param line    Line number, one-based.
 * @return The locus.
 */
[[nodiscard]] constexpr Locus lineLocus(core::u32 heading, core::u32 line) noexcept
{
    return Locus{0u, heading, line};
}

/**
 * @brief Writes a locus the way its corpus cites it.
 *
 * @param locus     The position.
 * @param plainText true to render a file position, false to render a work passage.
 * @param out       Receives a NUL-terminated reference.
 * @param capacity  Room in @p out, NUL included.
 * @return Bytes written, NUL excluded.
 */
[[nodiscard]] core::u32 renderLocus(const Locus &locus, bool plainText, char *out, core::u32 capacity) noexcept;

/**
 * @brief Does one locus come before another?
 *
 * Part, then section, then line — the reading order of the text, which is also the order
 * the baker sorts by so a reader can scan a work front to back.
 *
 * @param a First position.
 * @param b Second position.
 * @return true when @p a precedes @p b.
 */
[[nodiscard]] constexpr bool locusPrecedes(const Locus &a, const Locus &b) noexcept
{
    if (a.part != b.part)
        return a.part < b.part;
    if (a.section != b.section)
        return a.section < b.section;
    return a.line < b.line;
}

} // namespace lpl::corpus

#endif // LPL_LPL_CORPUS_LOCUS_HPP
