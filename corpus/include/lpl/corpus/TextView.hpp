/**
 * @file TextView.hpp
 * @brief A non-owning window into a baked text section.
 *
 * Texts are large and immutable; nothing should ever copy one to read it.
 *
 * The window is addressed by LINE and not by byte, and the line table was computed by the
 * baker. That is the same trade the whole reader half is built on: a scan for the nth
 * newline is O(bytes) and would be paid on every lookup by the least capable target,
 * whereas an offset table is paid once by the host that has a heap to build it with.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_CORPUS_TEXTVIEW_HPP
#    define LPL_LPL_CORPUS_TEXTVIEW_HPP

#    include <lpl/Foundation.hpp>

namespace lpl::corpus {

/**
 * @struct TextSectionHeaderV1
 * @brief Prologue of a baked Texts section.
 *
 * Wire layout, pinned like every other record that crosses the boundary.
 */
struct TextSectionHeaderV1 {
    core::u32 lineCount; ///< Lines in this section.
    core::u32 byteCount; ///< Bytes of text after the offset table.
};
static_assert(sizeof(TextSectionHeaderV1) == 8u, "TextSectionHeaderV1 layout is wire format");

/**
 * @class TextView
 * @brief Bounded, non-owning access to baked text.
 *
 * The offset table holds `lineCount + 1` entries, so the length of the last line is a
 * subtraction like every other. Storing lengths instead would have made the final line the
 * one case that needs a different rule, which is where an off-by-one lives.
 */
class TextView {
public:
    TextView() noexcept = default;

    /**
     * @brief Validates and adopts a baked Texts section.
     *
     * @param bytes First byte of the section.
     * @param size  Its length.
     * @return false when the section is malformed — short, or with a table that does not
     *         account for exactly the bytes it declares.
     */
    [[nodiscard]] bool open(const core::u8 *bytes, core::u32 size) noexcept;

    /**
     * @brief Lines the section holds.
     *
     * @return The count.
     */
    [[nodiscard]] core::u32 lineCount() const noexcept { return _lineCount; }

    /**
     * @brief One line, as a view.
     *
     * @param index    Line number, zero-based.
     * @param outBytes Receives the first byte; not NUL-terminated.
     * @param outSize  Receives its length.
     * @return false when @p index is out of range.
     */
    [[nodiscard]] bool line(core::u32 index, const char *&outBytes, core::u32 &outSize) const noexcept;

    /**
     * @brief Folds the whole text, byte for byte.
     *
     * @return The signature.
     */
    [[nodiscard]] core::u32 fold() const noexcept;

private:
    const core::u32 *_offsets{nullptr};
    const char *_text{nullptr};
    core::u32 _lineCount{0u};
    core::u32 _byteCount{0u};
};

} // namespace lpl::corpus

#endif // LPL_LPL_CORPUS_TEXTVIEW_HPP
