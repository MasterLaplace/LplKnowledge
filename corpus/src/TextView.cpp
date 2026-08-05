/**
 * @file TextView.cpp
 * @brief Implementation of a non-owning window into a baked text section.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/corpus/TextView.hpp>

namespace lpl::corpus {

namespace {

constexpr core::u32 kFnv1aOffsetBasis = 0x811C9DC5u;
constexpr core::u32 kFnv1aPrime = 0x01000193u;

/**
 * @brief Reads a little-endian word out of a byte range.
 *
 * @param bytes  First byte of the range.
 * @param offset Where the word starts.
 * @return The word.
 */
[[nodiscard]] core::u32 readWord(const core::u8 *bytes, core::u32 offset) noexcept
{
    return static_cast<core::u32>(bytes[offset]) | (static_cast<core::u32>(bytes[offset + 1u]) << 8) |
           (static_cast<core::u32>(bytes[offset + 2u]) << 16) | (static_cast<core::u32>(bytes[offset + 3u]) << 24);
}

} // namespace

bool TextView::open(const core::u8 *bytes, core::u32 size) noexcept
{
    _offsets = nullptr;
    _text = nullptr;
    _lineCount = 0u;
    _byteCount = 0u;

    if (bytes == nullptr || size < sizeof(TextSectionHeaderV1))
        return false;
    if ((reinterpret_cast<core::usize>(bytes) & 3u) != 0u)
        return false; // the offset table is read as words; a misaligned section is refused

    const core::u32 lineCount = readWord(bytes, 0u);
    const core::u32 byteCount = readWord(bytes, 4u);

    // lineCount + 1 entries, so the last line's length is a subtraction like every other.
    // Checked for overflow first: a declared count near the word limit would make the
    // table size wrap and compare small.
    if (lineCount > (0xFFFFFFFFu / 4u) - 2u)
        return false;
    const core::u32 tableBytes = (lineCount + 1u) * 4u;
    if (static_cast<core::u32>(sizeof(TextSectionHeaderV1)) + tableBytes + byteCount != size)
        return false;

    _offsets = reinterpret_cast<const core::u32 *>(bytes + sizeof(TextSectionHeaderV1));
    _text = reinterpret_cast<const char *>(bytes + sizeof(TextSectionHeaderV1) + tableBytes);
    _lineCount = lineCount;
    _byteCount = byteCount;

    // The table has to be monotonic and end exactly at the last byte, or a "line" could
    // name a negative length or reach past the text. Validated here, once, so that
    // @ref line is a lookup and not a second bounds check.
    core::u32 previous = 0u;
    for (core::u32 i = 0u; i <= lineCount; ++i)
    {
        const core::u32 offset = _offsets[i];
        if (offset < previous || offset > byteCount)
        {
            _offsets = nullptr;
            _text = nullptr;
            _lineCount = 0u;
            _byteCount = 0u;
            return false;
        }
        previous = offset;
    }
    if (lineCount != 0u && _offsets[lineCount] != byteCount)
    {
        _offsets = nullptr;
        _text = nullptr;
        _lineCount = 0u;
        _byteCount = 0u;
        return false;
    }

    return true;
}

bool TextView::line(core::u32 index, const char *&outBytes, core::u32 &outSize) const noexcept
{
    outBytes = nullptr;
    outSize = 0u;
    if (_offsets == nullptr || index >= _lineCount)
        return false;

    const core::u32 start = _offsets[index];
    const core::u32 end = _offsets[index + 1u];
    outBytes = _text + start;
    outSize = end - start;
    return true;
}

core::u32 TextView::fold() const noexcept
{
    core::u32 hash = kFnv1aOffsetBasis;
    for (core::u32 i = 0u; i < _byteCount; ++i)
        hash = (hash ^ static_cast<core::u32>(static_cast<core::u8>(_text[i]))) * kFnv1aPrime;
    hash = (hash ^ _lineCount) * kFnv1aPrime;
    hash = (hash ^ _byteCount) * kFnv1aPrime;
    return hash;
}

} // namespace lpl::corpus
