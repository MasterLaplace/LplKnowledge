/**
 * @file CatalogueStream.hpp
 * @brief Baking a catalogue larger than memory.
 *
 * @warning **This exists because of a measurement, not because of a plan.** `Baker` accumulates the
 * whole image before it writes a byte, which is right for a corpus of facts and wrong for a
 * catalogue of holdings. Measured: 126 650 HathiTrust rows cost **86 MB** of resident memory,
 * so the eighteen million rows of the full file project to **11.9 GB** — survivable on this
 * machine and only just — and Internet Archive's 51.6 million texts project to about 34 GB,
 * which is not survivable at all. The ceiling on how much of the world can be indexed was the
 * baker's memory, and nothing in the plans had noticed.
 *
 * **What makes streaming possible here, and would not elsewhere.** A catalogue bake writes two
 * growing sections — Texts and Catalogue — and BOTH are append-only and need no sorting. A
 * corpus of facts cannot be streamed the same way: `Baker` sorts facts into a canonical order
 * so that the same corpus bakes to the same bytes on two machines, and sorting needs the whole
 * of it. So this is not a replacement for `Baker`; it is the one shape of bake that happens to
 * be a stream, and it is the shape the immense corpus takes.
 *
 * **The one real obstacle, and how it is got round.** The Texts section stores its offset table
 * BEFORE its body, and the table is not known until the body is complete. Three temporary
 * files carry the three growing runs — text body, text offsets, catalogue rows — and the final
 * assembly concatenates them behind a header whose sizes are by then known. Memory stays flat;
 * the cost is paid in disk, which is the resource there is more of.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_CATALOGUESTREAM_HPP
#    define LPL_LPL_HARVEST_CATALOGUESTREAM_HPP

#    include <lpl/Foundation.hpp>
#    include <lpl/harvest/Baker.hpp>

#    include <cstdio>
#    include <string>
#    include <string_view>
#    include <vector>

namespace lpl::harvest {

/**
 * @brief Where part @p index of a partitioned catalogue lives.
 *
 * @warning **The convention is declared here so that the writer and the reader cannot drift.** A
 * partitioned bake is only usable if whoever reads it can find the other files, and the two ends
 * agreeing by coincidence is the duplication this repository keeps paying for. `CatalogueStream`
 * names its parts with this; `lpl-ask` finds its siblings with this.
 *
 * Part 0 is the path the caller asked for, exactly — so a catalogue that fitted produces one file
 * with the name they chose, and splitting stays invisible until it is needed.
 *
 * @param base  The path the bake was asked to write.
 * @param index Zero-based part number.
 * @return The path.
 */
[[nodiscard]] std::string cataloguePartPath(const std::string &base, core::u32 index);

/**
 * @class CatalogueStream
 * @brief An @ref ICatalogueSink that writes to disk as it goes.
 *
 * @warning The image it produces is byte-identical to the one `Baker` would produce from the same
 * calls, and that is asserted rather than assumed — otherwise there would be two writers of one
 * format, free to drift, which is the duplication this repository keeps paying for.
 */
class CatalogueStream final : public ICatalogueSink {
public:
    CatalogueStream() = default;
    ~CatalogueStream() override;

    CatalogueStream(const CatalogueStream &) = delete;
    CatalogueStream &operator=(const CatalogueStream &) = delete;

    /**
     * @brief Opens the stream, creating its temporaries beside the output.
     *
     * Beside the output rather than in a system temporary directory, deliberately: the runs are
     * as large as the image, and the place a caller chose to put a three-gigabyte file is the
     * place that has room for it.
     *
     * @param path Where the image will go.
     * @return false when the output or its temporaries could not be created.
     */
    [[nodiscard]] bool open(const std::string &path, core::u64 maxImageBytes = kImageCeiling);

    /**
     * Largest image this format can address, and therefore the default part size.
     *
     * The header's `totalSize` and every section offset are 32-bit words. Measured: the full
     * HathiTrust catalogue bakes to 3.71 GB, which is **93 %** of this — so the ceiling is not a
     * theoretical limit but the next one to be hit, and Internet Archive's 51.6 million texts
     * project to about 9.8 GB and would simply be refused.
     */
    static constexpr core::u64 kImageCeiling = 0xFFFFFFFFu;

    /**
     * Headroom reserved for one row when deciding whether to roll over.
     *
     * Generous on purpose: a title, a creator and an address plus a 32-byte record, and the
     * cost of over-reserving is a few unused bytes at the end of a part while the cost of
     * under-reserving is an image that overflows the format on its last row.
     */
    static constexpr core::u64 kRowReserve = 8192u;

    /**
     * @brief Records that an identifier reads as a name.
     *
     * Held in memory, unlike everything else here, and that is a bounded decision rather than
     * an inconsistency: a catalogue's vocabulary is its HOLDERS — a handful of repositories —
     * while its rows are millions. Naming rows is what this format does not do; see
     * `CatalogueEntryV1` for why every string is a line index instead.
     *
     * @param id   The identifier.
     * @param text What it reads as.
     * @return false on a collision, as @ref Baker::name.
     */
    [[nodiscard]] bool name(core::u32 id, std::string_view text);

    /**
     * @brief Appends one line of text.
     *
     * @param text The line.
     * @return Its zero-based index.
     */
    core::u32 addText(std::string_view text) override;

    /**
     * @brief Appends one catalogue entry.
     *
     * @warning **`cluster` is REBASED onto this part, and dropped when it cannot be.** It is a
     * one-based ROW INDEX, so it means a position in one image for exactly the reason the title,
     * creator and address do — and copying a number that was an index into the whole sequence
     * would, in part two, name whichever unrelated row happens to sit there. The rebase is
     * arithmetic the writer can do because it knows how many rows went before; what it cannot do
     * is express "the first copy of this work is in the previous file", so such a row goes back
     * to zero, which already means UNRESOLVED. Counted, never silent: see @ref clustersDropped.
     *
     * @param entry The row.
     */
    void addCatalogueEntry(const knowledge::CatalogueEntryV1 &entry) override;

    /**
     * @brief Rows whose resolved work was left behind by a part boundary.
     *
     * Non-zero only when a caller resolved before streaming AND the bake split. Reported so the
     * trade is visible and actionable — a larger part size, or resolving afterwards — rather
     * than being a number that quietly went missing.
     *
     * @return The count.
     */
    [[nodiscard]] core::u32 clustersDropped() const noexcept { return _clustersDropped; }

    /**
     * @brief Starts a new part when the row about to be written would not fit.
     *
     * The only place a part may end. See @ref ICatalogueSink::beginRow.
     */
    void beginRow() override;

    /**
     * @brief Assembles the image and closes the stream.
     *
     * @param report Receives what was written.
     * @return false when a write failed. A partially written image is REMOVED rather than
     *         left: a truncated `.lplknow` opens far enough to look like a small one.
     */
    [[nodiscard]] bool finish(BakeReport &report);

    /**
     * @brief How many images the bake produced.
     *
     * @warning More than one when the holdings did not fit. Splitting rather than widening the format:
     * the offsets are 32-bit in the wire record the ring-0 reader shares, so a 64-bit variant
     * would move every image already written and the parity gate with them. Every large corpus
     * in this field is distributed in parts already — Perseus in repositories, OpenITI in sixty
     * of them by century — so parts are the shape the material comes in anyway.
     *
     * @return The count; 1 for a catalogue that fitted.
     */
    [[nodiscard]] core::u32 parts() const noexcept { return _parts; }

    /**
     * @brief The path of one part.
     *
     * @param index Zero-based.
     * @return The path, or an empty string when @p index is past the end.
     */
    [[nodiscard]] std::string partPath(core::u32 index) const;

    /**
     * @brief Bytes of temporary run files currently on disk.
     *
     * Reported so the disk cost of the trade is visible, the memory cost having been the whole
     * reason for it.
     *
     * @return The total.
     */
    [[nodiscard]] core::u64 spilled() const noexcept { return _textBytes + _offsetBytes + _entryBytes; }

private:
    std::string _base; ///< The path the caller asked for; parts derive their names from it.

    /**
     * @brief Removes the temporaries.
     */
    void discard() noexcept;

    /**
     * @brief Opens the runs for the next part.
     *
     * @return false when they could not be created.
     */
    [[nodiscard]] bool openPart();

    /**
     * @brief Closes the current part, writing its image.
     *
     * @param report Receives what that part held.
     * @return false when a write failed.
     */
    [[nodiscard]] bool finishPart(BakeReport &report);

    /**
     * @brief Starts a new part when the current one would overflow.
     *
     * @param incoming Bytes the next row is about to add.
     * @return false when rolling over failed.
     */
    [[nodiscard]] bool rollIfFull(core::u64 incoming);

private:
    struct Named {
        core::u32 id;
        std::string text;
    };

    std::string _path;
    std::FILE *_textRun{nullptr};
    std::FILE *_offsetRun{nullptr};
    std::FILE *_entryRun{nullptr};
    std::string _textPath;
    std::string _offsetPath;
    std::string _entryPath;

    std::vector<Named> _names;
    core::u64 _ceiling{kImageCeiling};
    core::u32 _parts{0u};
    core::u64 _rowsBefore{0u};      ///< Rows written into earlier parts; see @ref addCatalogueEntry.
    core::u32 _clustersDropped{0u}; ///< Rows whose work lives in an earlier part.
    std::vector<std::string> _written;
    core::u32 _textCount{0u};
    core::u32 _entryCount{0u};
    core::u64 _textBytes{0u};
    core::u64 _offsetBytes{0u};
    core::u64 _entryBytes{0u};
    bool _failed{false};
};

} // namespace lpl::harvest

#endif // LPL_LPL_HARVEST_CATALOGUESTREAM_HPP
