/**
 * @file KnowledgePack.cpp
 * @brief Implementation of the .lplknow image: header, section table, sections.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/knowledge/KnowledgePack.hpp>

namespace lpl::knowledge {

namespace {

/// The eight bytes at the head of every image.
constexpr char kMagic[kMagicSize] = {'L', 'P', 'L', 'K', 'N', 'O', 'W', '\0'};

/**
 * @brief Reads a little-endian word out of a byte image.
 *
 * Assembled from bytes rather than dereferenced as a @c u32, because the header and the
 * table are read BEFORE alignment has been validated — which is the one place this
 * reader cannot assume what it is about to prove.
 *
 * @param bytes  First byte of the image.
 * @param offset Where the word starts.
 * @return The word.
 */
[[nodiscard]] core::u32 readWord(const core::u8 *bytes, core::u32 offset) noexcept
{
    return static_cast<core::u32>(bytes[offset]) | (static_cast<core::u32>(bytes[offset + 1u]) << 8) |
           (static_cast<core::u32>(bytes[offset + 2u]) << 16) | (static_cast<core::u32>(bytes[offset + 3u]) << 24);
}

/**
 * @brief Is a section extent inside the image, and aligned?
 *
 * @param offset Section start.
 * @param size   Section length.
 * @param total  Image length.
 * @return true when the extent is usable.
 */
[[nodiscard]] bool extentFits(core::u32 offset, core::u32 size, core::u32 total) noexcept
{
    // Written as a subtraction rather than as `offset + size <= total`: the sum of two
    // attacker-chosen words wraps, and a wrapped sum compares small.
    return offset <= total && size <= total - offset;
}

} // namespace

const char *openStatusText(OpenStatus status) noexcept
{
    switch (status)
    {
    case OpenStatus::Ok: return "ok";
    case OpenStatus::TooSmall: return "too small";
    case OpenStatus::BadMagic: return "bad magic";
    case OpenStatus::BadVersion: return "bad version";
    case OpenStatus::SizeMismatch: return "size mismatch";
    case OpenStatus::TableOutOfRange: return "section table out of range";
    case OpenStatus::SectionOutOfRange: return "section out of range";
    case OpenStatus::Misaligned: return "section misaligned";
    case OpenStatus::HashMismatch: return "content hash mismatch";
    case OpenStatus::ShortSection: return "short section";
    }
    return "unknown";
}

core::u32 foldImage(const core::u8 *bytes, core::u32 size) noexcept
{
    core::u32 hash = kFnv1aOffsetBasis;
    if (bytes != nullptr)
        foldBytes(hash, bytes, size);
    foldWord(hash, size);
    return hash;
}

void KnowledgePack::reset() noexcept
{
    _bytes = nullptr;
    _size = 0u;
    _sectionCount = 0u;
    _skippedSections = 0u;
    _facts = nullptr;
    _factCount = 0u;
    _sources = nullptr;
    _sourceCount = 0u;
    _documents = nullptr;
    _documentCount = 0u;
    _loci = nullptr;
    _locusCount = 0u;
    _vocabulary = nullptr;
    _vocabularyCount = 0u;
    _vocabularyText = nullptr;
    _vocabularyTextBytes = 0u;
}

OpenStatus KnowledgePack::open(const core::u8 *bytes, core::u32 size) noexcept
{
    reset();

    if (bytes == nullptr || size < sizeof(Header))
        return OpenStatus::TooSmall;

    for (core::u32 i = 0u; i < kMagicSize; ++i)
        if (static_cast<char>(bytes[i]) != kMagic[i])
            return OpenStatus::BadMagic;

    const core::u32 version = readWord(bytes, 8u);
    if (version != kFormatVersion)
        return OpenStatus::BadVersion;

    const core::u32 totalSize = readWord(bytes, 12u);
    if (totalSize != size)
        return OpenStatus::SizeMismatch;

    const core::u32 sectionCount = readWord(bytes, 16u);
    const core::u32 declaredHash = readWord(bytes, 20u);

    const core::u32 tableOffset = static_cast<core::u32>(sizeof(Header));
    if (!extentFits(tableOffset, sectionCount * static_cast<core::u32>(sizeof(SectionEntry)), size))
        return OpenStatus::TableOutOfRange;

    // The hash covers everything after the header, the section table included. Verified
    // BEFORE any extent is trusted: a damaged table is exactly what would send the
    // section walk below out of the image, and finding out afterwards is finding out too
    // late.
    core::u32 hash = kFnv1aOffsetBasis;
    foldBytes(hash, bytes + sizeof(Header), size - static_cast<core::u32>(sizeof(Header)));
    if (hash != declaredHash)
        return OpenStatus::HashMismatch;

    for (core::u32 i = 0u; i < sectionCount; ++i)
    {
        const core::u32 row = tableOffset + i * static_cast<core::u32>(sizeof(SectionEntry));
        const core::u32 type = readWord(bytes, row);
        const core::u32 offset = readWord(bytes, row + 4u);
        const core::u32 length = readWord(bytes, row + 8u);

        if (!extentFits(offset, length, size))
            return OpenStatus::SectionOutOfRange;
        if ((offset & 3u) != 0u)
            return OpenStatus::Misaligned;

        const core::u8 *payload = bytes + offset;

        switch (static_cast<SectionType>(type))
        {
        case SectionType::Facts:
            if (length % sizeof(FactV1) != 0u)
                return OpenStatus::ShortSection;
            _facts = reinterpret_cast<const FactV1 *>(payload);
            _factCount = length / static_cast<core::u32>(sizeof(FactV1));
            break;

        case SectionType::Sources:
            if (length % sizeof(SourceV1) != 0u)
                return OpenStatus::ShortSection;
            _sources = reinterpret_cast<const SourceV1 *>(payload);
            _sourceCount = length / static_cast<core::u32>(sizeof(SourceV1));
            break;

        case SectionType::Documents:
            if (length % sizeof(DocumentV1) != 0u)
                return OpenStatus::ShortSection;
            _documents = reinterpret_cast<const DocumentV1 *>(payload);
            _documentCount = length / static_cast<core::u32>(sizeof(DocumentV1));
            break;

        case SectionType::Loci:
            if (length % sizeof(LocusV1) != 0u)
                return OpenStatus::ShortSection;
            _loci = reinterpret_cast<const LocusV1 *>(payload);
            _locusCount = length / static_cast<core::u32>(sizeof(LocusV1));
            break;

        case SectionType::Vocabulary: {
            if (length < sizeof(VocabularyHeaderV1))
                return OpenStatus::ShortSection;
            const core::u32 count = readWord(bytes, offset);
            const core::u32 textBytes = readWord(bytes, offset + 4u);
            const core::u32 entryBytes = count * static_cast<core::u32>(sizeof(VocabularyEntryV1));
            // Every part accounted for exactly: prologue, entries, text. An image whose
            // section is LONGER than its contents is as suspect as one that is shorter —
            // the surplus is bytes nothing describes.
            if (static_cast<core::u32>(sizeof(VocabularyHeaderV1)) + entryBytes + textBytes != length)
                return OpenStatus::ShortSection;
            _vocabularyCount = count;
            _vocabulary =
                reinterpret_cast<const VocabularyEntryV1 *>(payload + sizeof(VocabularyHeaderV1));
            _vocabularyText = reinterpret_cast<const char *>(payload + sizeof(VocabularyHeaderV1) + entryBytes);
            _vocabularyTextBytes = textBytes;
            break;
        }

        // Texts and Ecc are extents this reader locates on request and does not
        // interpret; anything else is a section from a newer writer.
        case SectionType::Texts:
        case SectionType::Ecc: break;

        case SectionType::Unknown:
        default: ++_skippedSections; break;
        }
    }

    _bytes = bytes;
    _size = size;
    _sectionCount = sectionCount;
    return OpenStatus::Ok;
}

bool KnowledgePack::section(SectionType type, const core::u8 *&outBytes, core::u32 &outSize) const noexcept
{
    outBytes = nullptr;
    outSize = 0u;
    if (_bytes == nullptr)
        return false;

    const core::u32 tableOffset = static_cast<core::u32>(sizeof(Header));
    for (core::u32 i = 0u; i < _sectionCount; ++i)
    {
        const core::u32 row = tableOffset + i * static_cast<core::u32>(sizeof(SectionEntry));
        if (static_cast<SectionType>(readWord(_bytes, row)) != type)
            continue;
        outBytes = _bytes + readWord(_bytes, row + 4u);
        outSize = readWord(_bytes, row + 8u);
        return true;
    }
    return false;
}

bool KnowledgePack::hasSection(SectionType type) const noexcept
{
    const core::u8 *ignored = nullptr;
    core::u32 size = 0u;
    return section(type, ignored, size);
}

bool KnowledgePack::factAt(core::u32 index, FactV1 &out) const noexcept
{
    if (_facts == nullptr || index >= _factCount)
        return false;
    out = _facts[index];
    return true;
}

bool KnowledgePack::sourceAt(core::u32 index, SourceV1 &out) const noexcept
{
    if (_sources == nullptr || index >= _sourceCount)
        return false;
    out = _sources[index];
    return true;
}

bool KnowledgePack::sourceById(core::u32 id, SourceV1 &out) const noexcept
{
    for (core::u32 i = 0u; i < _sourceCount; ++i)
    {
        if (_sources[i].id != id)
            continue;
        out = _sources[i];
        return true;
    }
    return false;
}

bool KnowledgePack::documentAt(core::u32 index, DocumentV1 &out) const noexcept
{
    if (_documents == nullptr || index >= _documentCount)
        return false;
    out = _documents[index];
    return true;
}

bool KnowledgePack::locusAt(core::u32 index, LocusV1 &out) const noexcept
{
    if (_loci == nullptr || index >= _locusCount)
        return false;
    out = _loci[index];
    return true;
}

const char *KnowledgePack::textFor(core::u32 id) const noexcept
{
    if (_vocabulary == nullptr || _vocabularyCount == 0u)
        return nullptr;

    core::u32 low = 0u;
    core::u32 high = _vocabularyCount;
    while (low < high)
    {
        const core::u32 middle = low + (high - low) / 2u;
        const core::u32 candidate = _vocabulary[middle].id;
        if (candidate == id)
        {
            const core::u32 offset = _vocabulary[middle].textOffset;
            if (offset >= _vocabularyTextBytes)
                return nullptr;
            return _vocabularyText + offset;
        }
        if (candidate < id)
            low = middle + 1u;
        else
            high = middle;
    }
    return nullptr;
}

core::u32 KnowledgePack::fold() const noexcept { return foldImage(_bytes, _size); }

} // namespace lpl::knowledge
