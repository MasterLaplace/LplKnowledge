/**
 * @file CatalogueStream.cpp
 * @brief Implementation of baking a catalogue larger than memory.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/CatalogueStream.hpp>

#include <lpl/knowledge/Types.hpp>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace lpl::harvest {

namespace {

/**
 * @brief Writes one little-endian word to a stream.
 *
 * Explicit rather than a struct write: the wire is little-endian by contract, and a memcpy of
 * a native word would make the image depend on the machine that baked it.
 *
 * @param file Where.
 * @param word What.
 * @return false when the write failed.
 */
[[nodiscard]] bool putWord(std::FILE *file, core::u32 word) noexcept
{
    const unsigned char bytes[4] = {static_cast<unsigned char>(word & 0xFFu),
                                    static_cast<unsigned char>((word >> 8) & 0xFFu),
                                    static_cast<unsigned char>((word >> 16) & 0xFFu),
                                    static_cast<unsigned char>((word >> 24) & 0xFFu)};
    return std::fwrite(bytes, 1u, 4u, file) == 4u;
}

/**
 * @brief Appends one word to a byte buffer, little-endian.
 *
 * @param out  Destination.
 * @param word What.
 */
void pushWord(std::vector<core::u8> &out, core::u32 word)
{
    out.push_back(static_cast<core::u8>(word & 0xFFu));
    out.push_back(static_cast<core::u8>((word >> 8) & 0xFFu));
    out.push_back(static_cast<core::u8>((word >> 16) & 0xFFu));
    out.push_back(static_cast<core::u8>((word >> 24) & 0xFFu));
}

/**
 * @brief Copies a whole file into another, folding the bytes as it goes.
 *
 * The fold is computed DURING the copy rather than by a second pass, because a second pass
 * over three gigabytes is a second pass over three gigabytes.
 *
 * @param from Source, rewound by the caller.
 * @param to   Destination.
 * @param hash Running content hash, updated in place.
 * @return false when a read or write failed.
 */
[[nodiscard]] bool copyFolding(std::FILE *from, std::FILE *to, core::u32 &hash)
{
    std::vector<core::u8> buffer(1u << 20);
    std::size_t read = 0u;
    while ((read = std::fread(buffer.data(), 1u, buffer.size(), from)) > 0u)
    {
        knowledge::foldBytes(hash, buffer.data(), static_cast<core::u32>(read));
        if (std::fwrite(buffer.data(), 1u, read, to) != read)
            return false;
    }
    return std::ferror(from) == 0;
}

} // namespace

CatalogueStream::~CatalogueStream() { discard(); }

void CatalogueStream::discard() noexcept
{
    for (std::FILE **file : {&_textRun, &_offsetRun, &_entryRun})
    {
        if (*file != nullptr)
        {
            (void) std::fclose(*file);
            *file = nullptr;
        }
    }
    for (const std::string *path : {&_textPath, &_offsetPath, &_entryPath})
        if (!path->empty())
            (void) std::remove(path->c_str());
    _textPath.clear();
    _offsetPath.clear();
    _entryPath.clear();
}

std::string cataloguePartPath(const std::string &base, core::u32 index)
{
    if (index == 0u)
        return base;
    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), ".part%03u", index);
    return base + suffix;
}

bool CatalogueStream::open(const std::string &path, core::u64 maxImageBytes)
{
    discard();
    _base = path;
    _ceiling = maxImageBytes;
    _parts = 0u;
    _rowsBefore = 0u;
    _clustersDropped = 0u;
    _written.clear();
    return openPart();
}

std::string CatalogueStream::partPath(core::u32 index) const
{
    return index < _written.size() ? _written[index] : std::string{};
}

bool CatalogueStream::openPart()
{
    _path = cataloguePartPath(_base, _parts);
    _failed = false;
    _textCount = 0u;
    _entryCount = 0u;
    _textBytes = 0u;
    _offsetBytes = 0u;
    _entryBytes = 0u;
    // @warning `_names` is NOT cleared. The vocabulary names the holders, and every part has to name
    // its own — a part whose rows point at a holder identifier nothing in that image resolves
    // would print a number where a repository should be.

    // Beside the output, not in a system temporary directory: these runs are as large as the
    // image, and the directory a caller chose for a three-gigabyte file is the one with room.
    _textPath = _path + ".text.run";
    _offsetPath = _path + ".offset.run";
    _entryPath = _path + ".entry.run";

    _textRun = std::fopen(_textPath.c_str(), "w+b");
    _offsetRun = std::fopen(_offsetPath.c_str(), "w+b");
    _entryRun = std::fopen(_entryPath.c_str(), "w+b");
    if (_textRun == nullptr || _offsetRun == nullptr || _entryRun == nullptr)
    {
        discard();
        return false;
    }
    return true;
}

bool CatalogueStream::name(core::u32 id, std::string_view text)
{
    for (Named &entry : _names)
    {
        if (entry.id != id)
            continue;
        return entry.text == text; // named twice with the same word is idempotent
    }
    _names.push_back(Named{id, std::string{text}});
    return true;
}

void CatalogueStream::beginRow()
{
    // A row's worth of headroom, not one string's: the projection has to cover everything the
    // row is about to write, or the roll happens one string too late.
    if (!rollIfFull(kRowReserve))
        _failed = true;
}

bool CatalogueStream::rollIfFull(core::u64 incoming)
{
    // Projected size of the image as it stands, plus what is about to be added. Section
    // prologues, the offset sentinel and the header/table are counted in: a projection that
    // under-counts is a projection that overflows on the last row.
    const core::u64 projected = 128u + _textBytes + _offsetBytes + 4u + _entryBytes + incoming;
    if (projected <= _ceiling || _entryCount == 0u)
        return true;

    // @warning Rolled at a ROW boundary, never inside one. A row's title, creator and address are
    // Texts LINE INDICES, and an index means a position in THIS image — so a row that landed in
    // one part while its strings landed in another would resolve to whatever text happened to
    // sit at that index. Silently, and about a book.
    BakeReport ignored{};
    const core::u32 closing = _entryCount;
    if (!finishPart(ignored))
        return false;
    // Counted BEFORE the next part opens, because `addCatalogueEntry` rebases against it: a row
    // index is global on the way in and local on the way out, and the offset between the two is
    // exactly the rows already written.
    _rowsBefore += closing;
    ++_parts;
    return openPart();
}

core::u32 CatalogueStream::addText(std::string_view text)
{
    const core::u32 index = _textCount;
    if (_failed || _textRun == nullptr)
        return index;

    // The offset of this line, recorded before the body grows. The two runs are written in
    // lockstep so that neither can be ahead of the other if a write fails mid-way.
    if (!putWord(_offsetRun, static_cast<core::u32>(_textBytes)))
    {
        _failed = true;
        return index;
    }
    _offsetBytes += 4u;

    // @warning No terminator. The Texts section delimits a line by the NEXT offset, with a sentinel
    // past the last — so a NUL here would be a byte of content, and every line would come back
    // one character too long. Discovered by comparing against `Baker` rather than by reading it:
    // the two encodings produced images of the same size and different bytes.
    if (!text.empty() && std::fwrite(text.data(), 1u, text.size(), _textRun) != text.size())
    {
        _failed = true;
        return index;
    }
    _textBytes += text.size();
    ++_textCount;
    return index;
}

void CatalogueStream::addCatalogueEntry(const knowledge::CatalogueEntryV1 &entry)
{
    if (_failed || _entryRun == nullptr)
        return;

    // @warning Rebased onto this part. `cluster` is a one-based row index, so it means a position in
    // ONE image, exactly as the three text indices beside it do — writing the global number into
    // part two would name whichever unrelated row sits at that position. When the work's first
    // copy was left in an earlier part the relation is simply not expressible here, so the field
    // goes back to zero, which already means UNRESOLVED rather than "alone".
    core::u32 cluster = entry.cluster;
    if (cluster != knowledge::kNoIdentifier && _rowsBefore != 0u)
    {
        if (static_cast<core::u64>(cluster) > _rowsBefore)
            cluster = static_cast<core::u32>(static_cast<core::u64>(cluster) - _rowsBefore);
        else
        {
            cluster = knowledge::kNoIdentifier;
            ++_clustersDropped;
        }
    }

    // @warning Nine words, in the order `catalogueAt` reads them back. Three sites must agree on
    // this layout -- this writer, `Baker`, and the reader -- and the byte-for-byte comparison in
    // `test-catalogue` is what proves they do rather than a comment saying they should.
    const core::u32 words[9] = {entry.title,
                                entry.creator,
                                entry.address,
                                entry.holder,
                                static_cast<core::u32>(entry.year),
                                entry.language,
                                entry.flags,
                                cluster,
                                static_cast<core::u32>(entry.yearTo)};
    for (const core::u32 word : words)
    {
        if (!putWord(_entryRun, word))
        {
            _failed = true;
            return;
        }
    }
    _entryBytes += sizeof(words);
    ++_entryCount;
}

bool CatalogueStream::finish(BakeReport &report)
{
    if (!finishPart(report))
        return false;
    ++_parts;
    return true;
}

bool CatalogueStream::finishPart(BakeReport &report)
{
    report = BakeReport{};
    if (_failed || _textRun == nullptr)
    {
        discard();
        return false;
    }

    // ── The vocabulary, which is small enough to hold ─────────────────────────
    std::vector<Named> sorted = _names;
    std::sort(sorted.begin(), sorted.end(), [](const Named &a, const Named &b) { return a.id < b.id; });
    std::vector<core::u8> vocabulary;
    if (!sorted.empty())
    {
        std::vector<core::u8> body;
        std::vector<core::u32> offsets;
        for (const Named &entry : sorted)
        {
            offsets.push_back(static_cast<core::u32>(body.size()));
            body.insert(body.end(), entry.text.begin(), entry.text.end());
            body.push_back(0u);
        }
        pushWord(vocabulary, static_cast<core::u32>(sorted.size()));
        pushWord(vocabulary, static_cast<core::u32>(body.size()));
        // @warning A vocabulary entry is a PAIR — identifier and offset — not a bare offset, and its
        // text IS NUL-terminated because the reader hands back a C string. The Texts section a
        // few lines below is the opposite on both counts. Two sections, two encodings, and the
        // only way to be sure of either is to write the same bytes the other writer writes.
        for (std::size_t i = 0u; i < sorted.size(); ++i)
        {
            pushWord(vocabulary, sorted[i].id);
            pushWord(vocabulary, offsets[i]);
        }
        vocabulary.insert(vocabulary.end(), body.begin(), body.end());
    }

    // ── Sizes, now that every run is complete ────────────────────────────────
    // count + 1 offsets: the sentinel is what gives the last line an end.
    const core::u64 textSection = _textCount == 0u ? 0u : 8u + _offsetBytes + 4u + _textBytes;
    const core::u64 catalogueSection = _entryCount == 0u ? 0u : 8u + _entryBytes;

    core::u32 sectionCount = 0u;
    if (!vocabulary.empty())
        ++sectionCount;
    if (_textCount != 0u)
        ++sectionCount;
    if (_entryCount != 0u)
        ++sectionCount;

    const core::u64 headerBytes = sizeof(knowledge::Header);
    const core::u64 tableBytes = sectionCount * sizeof(knowledge::SectionEntry);

    // @warning The format's offsets and total size are 32-bit words, so an image cannot exceed four
    // gigabytes. Refused here rather than truncated: a wrapped offset points into the middle
    // of a section and the reader would open it and answer questions wrongly. Internet
    // Archive's 51.6 million rows would land near this, which is the next wall after memory.
    core::u64 cursor = headerBytes + tableBytes;
    std::vector<core::u64> offsets;
    for (const core::u64 size : {static_cast<core::u64>(vocabulary.size()), textSection, catalogueSection})
    {
        if (size == 0u)
            continue;
        cursor = (cursor + 3u) & ~static_cast<core::u64>(3u);
        offsets.push_back(cursor);
        cursor += size;
    }
    if (cursor > 0xFFFFFFFFu)
    {
        discard();
        return false;
    }
    const core::u32 totalSize = static_cast<core::u32>(cursor);

    std::FILE *out = std::fopen(_path.c_str(), "wb");
    if (out == nullptr)
    {
        discard();
        return false;
    }

    core::u32 hash = knowledge::kFnv1aOffsetBasis;
    bool ok = true;

    // Header. The content hash covers everything after it, so it is written as zero and
    // patched once the bytes it covers exist.
    const char magic[] = {'L', 'P', 'L', 'K', 'N', 'O', 'W', '\0'};
    ok = ok && std::fwrite(magic, 1u, sizeof(magic), out) == sizeof(magic);
    ok = ok && putWord(out, knowledge::kFormatVersion);
    ok = ok && putWord(out, totalSize);
    ok = ok && putWord(out, sectionCount);
    ok = ok && putWord(out, 0u);
    ok = ok && putWord(out, 0u);
    ok = ok && putWord(out, 0u);

    // Section table, folded as it is written.
    {
        std::vector<core::u8> table;
        std::size_t which = 0u;
        const knowledge::SectionType types[3] = {knowledge::SectionType::Vocabulary,
                                                 knowledge::SectionType::Texts,
                                                 knowledge::SectionType::Catalogue};
        const core::u64 sizes[3] = {static_cast<core::u64>(vocabulary.size()), textSection, catalogueSection};
        for (std::size_t i = 0u; i < 3u; ++i)
        {
            if (sizes[i] == 0u)
                continue;
            pushWord(table, static_cast<core::u32>(types[i]));
            pushWord(table, static_cast<core::u32>(offsets[which]));
            pushWord(table, static_cast<core::u32>(sizes[i]));
            pushWord(table, 0u);
            ++which;
        }
        knowledge::foldBytes(hash, table.data(), static_cast<core::u32>(table.size()));
        ok = ok && std::fwrite(table.data(), 1u, table.size(), out) == table.size();
    }

    // Padding to each section's four-byte boundary, folded like everything else.
    const auto pad = [&](core::u64 to) {
        core::u64 at = static_cast<core::u64>(std::ftell(out));
        while (ok && at < to)
        {
            const core::u8 zero = 0u;
            knowledge::foldBytes(hash, &zero, 1u);
            ok = std::fwrite(&zero, 1u, 1u, out) == 1u;
            ++at;
        }
    };

    std::size_t which = 0u;
    if (!vocabulary.empty())
    {
        pad(offsets[which++]);
        knowledge::foldBytes(hash, vocabulary.data(), static_cast<core::u32>(vocabulary.size()));
        ok = ok && std::fwrite(vocabulary.data(), 1u, vocabulary.size(), out) == vocabulary.size();
    }
    if (_textCount != 0u)
    {
        pad(offsets[which++]);
        std::vector<core::u8> prologue;
        pushWord(prologue, _textCount);
        pushWord(prologue, static_cast<core::u32>(_textBytes));
        knowledge::foldBytes(hash, prologue.data(), static_cast<core::u32>(prologue.size()));
        ok = ok && std::fwrite(prologue.data(), 1u, prologue.size(), out) == prologue.size();
        ok = ok && std::fseek(_offsetRun, 0, SEEK_SET) == 0 && copyFolding(_offsetRun, out, hash);
        // The sentinel, appended once the body length is final.
        {
            std::vector<core::u8> sentinel;
            pushWord(sentinel, static_cast<core::u32>(_textBytes));
            knowledge::foldBytes(hash, sentinel.data(), 4u);
            ok = ok && std::fwrite(sentinel.data(), 1u, 4u, out) == 4u;
        }
        ok = ok && std::fseek(_textRun, 0, SEEK_SET) == 0 && copyFolding(_textRun, out, hash);
    }
    if (_entryCount != 0u)
    {
        pad(offsets[which++]);
        std::vector<core::u8> prologue;
        pushWord(prologue, _entryCount);
        pushWord(prologue, 0u);
        knowledge::foldBytes(hash, prologue.data(), static_cast<core::u32>(prologue.size()));
        ok = ok && std::fwrite(prologue.data(), 1u, prologue.size(), out) == prologue.size();
        ok = ok && std::fseek(_entryRun, 0, SEEK_SET) == 0 && copyFolding(_entryRun, out, hash);
    }

    // Patch the content hash into the header now that the bytes it covers are written.
    ok = ok && std::fseek(out, 20, SEEK_SET) == 0 && putWord(out, hash);
    ok = std::fclose(out) == 0 && ok;

    if (!ok)
    {
        // A truncated `.lplknow` opens far enough to look like a small one, so a failed bake
        // leaves nothing rather than something plausible.
        (void) std::remove(_path.c_str());
        discard();
        return false;
    }

    report.catalogue = _entryCount;
    report.textLines = _textCount;
    report.textBytes = static_cast<core::u32>(_textBytes);
    report.vocabulary = static_cast<core::u32>(sorted.size());
    report.sections = sectionCount;
    report.bytes = totalSize;
    _written.push_back(_path);
    discard();
    return true;
}

} // namespace lpl::harvest
