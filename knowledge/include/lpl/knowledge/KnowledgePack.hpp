/**
 * @file KnowledgePack.hpp
 * @brief The .lplknow image: header, section table, sections.
 *
 * The archival sibling of .lplpak, and deliberately the same shape. One format
 * family means one reader discipline, one set of mistakes already made and fixed.
 *
 * The reader is NON-OWNING and allocates nothing: it holds a pointer to bytes
 * somebody else is responsible for, validates before it believes anything, and
 * returns views. That is not frugality for its own sake — an image is an UNTRUSTED
 * input, ring 0 is the least capable reader there is, and a reader that allocated
 * would have a failure mode (out of memory) on a path whose whole job is to survive
 * a bad input.
 *
 * @warning Alignment is VALIDATED, not assumed. Every section offset must be a multiple of
 * four, and @ref KnowledgePack::open refuses an image where one is not. x86 would have
 * read a misaligned record without complaining, which is exactly why the check has to
 * be explicit: the target that tolerates the mistake is the target that hides it until
 * someone builds for one that does not.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_KNOWLEDGE_KNOWLEDGEPACK_HPP
#    define LPL_LPL_KNOWLEDGE_KNOWLEDGEPACK_HPP

#    include <lpl/Foundation.hpp>
#    include <lpl/knowledge/Types.hpp>

namespace lpl::knowledge {

/**
 * @enum OpenStatus
 * @brief Why an image was refused, when it was.
 *
 * A word for each failure rather than one boolean, because "this is not a knowledge
 * image" and "this is a knowledge image that has been damaged" call for opposite
 * responses: the first is a wrong file, the second is a repair. Collapsing them is how
 * a loader ends up silently falling back, which DESIGN §16.3 forbids.
 */
enum class OpenStatus : core::u32 {
    Ok = 0u,
    TooSmall = 1u,        ///< Not even a header's worth of bytes.
    BadMagic = 2u,        ///< Not a .lplknow image at all.
    BadVersion = 3u,      ///< A format this reader does not know.
    SizeMismatch = 4u,    ///< The header's declared size is not the buffer's size.
    TableOutOfRange = 5u, ///< The section table does not fit inside the image.
    SectionOutOfRange = 6u, ///< A section's extent leaves the image.
    Misaligned = 7u,      ///< A section offset is not a multiple of four.
    HashMismatch = 8u,    ///< The content hash does not match the content.
    ShortSection = 9u,    ///< A section is not a whole number of its records.
    UnterminatedText = 10u, ///< The vocabulary text does not end in a NUL, so textFor would read past it.
};

/**
 * @brief A word for an open status, for a log line.
 *
 * @param status What happened.
 * @return A short, stable string.
 */
[[nodiscard]] const char *openStatusText(OpenStatus status) noexcept;

/**
 * @class KnowledgePack
 * @brief Bounded, non-owning reader over one baked image.
 */
class KnowledgePack {
public:
    KnowledgePack() noexcept = default;

    /**
     * @brief Validates and adopts a byte image.
     *
     * Validation happens ONCE, here, and every accessor afterwards is a bounds-checked
     * read into already-verified extents. The alternative — checking on each access —
     * spreads the same test over twenty call sites and guarantees one of them is
     * eventually written without it.
     *
     * @param bytes First byte of the image.
     * @param size  How many bytes are there.
     * @return @ref OpenStatus::Ok when the image is usable; the reason otherwise.
     */
    [[nodiscard]] OpenStatus open(const core::u8 *bytes, core::u32 size) noexcept;

    /**
     * @brief Is a usable image adopted?
     *
     * @return true when @ref open last succeeded.
     */
    [[nodiscard]] bool ready() const noexcept { return _bytes != nullptr; }

    /**
     * @brief Locates one section.
     *
     * @param type       Which section.
     * @param outBytes   Receives its first byte.
     * @param outSize    Receives its length.
     * @return false when the image does not carry that section.
     */
    [[nodiscard]] bool section(SectionType type, const core::u8 *&outBytes, core::u32 &outSize) const noexcept;

    /**
     * @brief Does the image carry a section of this type?
     *
     * @param type Which section.
     * @return true when it is present.
     */
    [[nodiscard]] bool hasSection(SectionType type) const noexcept;

    /**
     * @brief Claims the image asserts.
     *
     * @return How many facts the Facts section holds.
     */
    [[nodiscard]] core::u32 factCount() const noexcept { return _factCount; }

    /**
     * @brief One claim.
     *
     * Copied out rather than returned by pointer, so a caller cannot accidentally hold
     * a reference into a buffer whose lifetime it does not control — the commonest way
     * a non-owning reader becomes a dangling one.
     *
     * @param index Which claim, in baked order.
     * @param out   Receives it.
     * @return false when @p index is out of range.
     */
    [[nodiscard]] bool factAt(core::u32 index, FactV1 &out) const noexcept;

    /**
     * @brief Sources the image describes.
     *
     * @return How many.
     */
    [[nodiscard]] core::u32 sourceCount() const noexcept { return _sourceCount; }

    /**
     * @brief Pairs somebody should look at, none of them merged.
     *
     * @return How many.
     */
    [[nodiscard]] core::u32 candidateCount() const noexcept { return _candidateCount; }

    /**
     * @brief Credits this image must carry to be redistributable.
     *
     * @return How many.
     */
    [[nodiscard]] core::u32 attributionCount() const noexcept { return _attributionCount; }

    /**
     * @brief Reads one credit.
     *
     * @param index Zero-based.
     * @param out   Receives it.
     * @return false when the index is past the end.
     */
    [[nodiscard]] bool attributionAt(core::u32 index, AttributionV1 &out) const noexcept;

    /**
     * @brief Reads one candidate pair.
     *
     * @param index Zero-based.
     * @param out   Receives it.
     * @return false when the index is past the end.
     */
    [[nodiscard]] bool candidateAt(core::u32 index, CandidateV1 &out) const noexcept;

    /**
     * @brief Whether the image carries measured ground.
     *
     * @return true when a relief section is present and consistent.
     */
    [[nodiscard]] bool hasRelief() const noexcept { return _relief != nullptr; }

    /**
     * @brief Reads the relief header: the projection and the shape of the samples.
     *
     * @param out Receives it.
     * @return false when the image carries no relief.
     */
    [[nodiscard]] bool relief(ReliefV1 &out) const noexcept;

    /**
     * @brief The samples themselves, `width * height` of them, north row first.
     *
     * @warning Non-owning, and pointing straight into the mapped image. In ring 0 this is a window
     * onto a section that was never copied, which is the whole reason the format stores them
     * already resampled onto cells.
     *
     * @return The first sample, or nullptr when the image carries no relief.
     */
    [[nodiscard]] const core::i16 *reliefSamples() const noexcept { return _reliefSamples; }

    /**
     * @brief One source profile.
     *
     * @param index Which source, in baked order.
     * @param out   Receives it.
     * @return false when @p index is out of range.
     */
    [[nodiscard]] bool sourceAt(core::u32 index, SourceV1 &out) const noexcept;

    /**
     * @brief Looks a source up by its identifier.
     *
     * @param id  The identifier a fact carries.
     * @param out Receives the profile.
     * @return false when the image does not describe that source.
     */
    [[nodiscard]] bool sourceById(core::u32 id, SourceV1 &out) const noexcept;

    /**
     * @brief Documents the image describes.
     *
     * @return How many.
     */
    [[nodiscard]] core::u32 documentCount() const noexcept { return _documentCount; }

    /**
     * @brief One document record.
     *
     * @param index Which document.
     * @param out   Receives it.
     * @return false when @p index is out of range.
     */
    [[nodiscard]] bool documentAt(core::u32 index, DocumentV1 &out) const noexcept;

    /**
     * @brief Loci the image describes.
     *
     * @return How many.
     */
    [[nodiscard]] core::u32 locusCount() const noexcept { return _locusCount; }

    /**
     * @brief One locus record.
     *
     * @param index Which locus.
     * @param out   Receives it.
     * @return false when @p index is out of range.
     */
    [[nodiscard]] bool locusAt(core::u32 index, LocusV1 &out) const noexcept;

    /**
     * @brief What an identifier reads as, for a human.
     *
     * Binary search over the baked, sorted vocabulary. The absence of an entry is
     * NORMAL and not an error: an identifier is the identity, and its spelling is an
     * annotation an image may legitimately have been baked without — a ring-0 image
     * that carries no vocabulary still answers every query correctly.
     *
     * @param id The identifier.
     * @return A NUL-terminated view into the image, or nullptr when unnamed.
     */
    [[nodiscard]] const char *textFor(core::u32 id) const noexcept;

    /**
     * @brief Entries in the vocabulary.
     *
     * @return How many identifiers the image names.
     */
    [[nodiscard]] core::u32 vocabularyCount() const noexcept { return _vocabularyCount; }

    /**
     * @brief How many catalogue entries the image carries.
     *
     * @return The count; zero when the image has no catalogue, which is the normal case for
     *         an image of facts rather than of holdings.
     */
    [[nodiscard]] core::u32 catalogueCount() const noexcept { return _catalogueCount; }

    /**
     * @brief Reads one catalogue entry.
     *
     * @param index Zero-based.
     * @param out   Receives the entry.
     * @return false when @p index is past the end.
     */
    [[nodiscard]] bool catalogueAt(core::u32 index, CatalogueEntryV1 &out) const noexcept;

    /**
     * @brief How many named places the image carries.
     *
     * @return The count; zero when it holds no gazetteer.
     */
    [[nodiscard]] core::u32 gazetteerCount() const noexcept { return _gazetteerCount; }

    /**
     * @brief Reads one place.
     *
     * @param index Zero-based.
     * @param out   Receives the entry.
     * @return false when @p index is past the end.
     */
    [[nodiscard]] bool gazetteerAt(core::u32 index, GazetteerEntryV1 &out) const noexcept;

    /**
     * @brief Finds a place by its repository identifier.
     *
     * @warning By identifier, never by name: ten distinct places are called "Alexandria". A linear scan
     * because a gazetteer is tens of thousands of rows, not millions, and a second index would be
     * a second structure to keep true.
     *
     * @param place The identifier.
     * @param out   Receives the entry.
     * @return false when no entry carries it.
     */
    [[nodiscard]] bool placeById(core::u32 place, GazetteerEntryV1 &out) const noexcept;

    /**
     * @brief How many attested connections the image carries.
     *
     * @return The count, both directions included.
     */
    [[nodiscard]] core::u32 placeLinkCount() const noexcept { return _placeLinkCount; }

    /**
     * @brief Reads one connection.
     *
     * @param index Zero-based.
     * @param out   Receives it.
     * @return false when @p index is past the end.
     */
    [[nodiscard]] bool placeLinkAt(core::u32 index, PlaceLinkV1 &out) const noexcept;

    /**
     * @brief Collects the places a given one connects to.
     *
     * @warning Bounded by @p capacity and the overflow is REPORTED, not silently dropped: a traveller
     * offered three of a crossroads' eight roads would walk a corpus nobody wrote.
     *
     * @param place    The identifier to look up.
     * @param out      Receives the neighbours.
     * @param capacity Room in @p out.
     * @param outTotal Receives how many there are, which may exceed @p capacity.
     * @return How many were written.
     */
    [[nodiscard]] core::u32 linksFrom(core::u32 place, core::u32 *out, core::u32 capacity,
                                      core::u32 &outTotal) const noexcept;

    /**
     * @brief Bytes of the whole image.
     *
     * @return The declared and verified total size.
     */
    [[nodiscard]] core::u32 size() const noexcept { return _size; }

    /**
     * @brief First byte of the image.
     *
     * @return The adopted pointer, or nullptr.
     */
    [[nodiscard]] const core::u8 *bytes() const noexcept { return _bytes; }

    /**
     * @brief Sections the table declares, understood or not.
     *
     * @return The count from the header.
     */
    [[nodiscard]] core::u32 sectionCount() const noexcept { return _sectionCount; }

    /**
     * @brief Sections whose type this reader does not know.
     *
     * Counted rather than ignored, because "this image carries something I skipped" is
     * a fact worth reporting even though it is not an error.
     *
     * @return How many were skipped.
     */
    [[nodiscard]] core::u32 skippedSections() const noexcept { return _skippedSections; }

    /**
     * @brief Folds the whole image byte for byte.
     *
     * The format's own signature: two hosts that bake the same corpus must produce the
     * same bytes, and this is what says so. Independent of every query, on purpose —
     * a fold over query results would move whenever a query is tuned.
     *
     * @return The signature.
     */
    [[nodiscard]] core::u32 fold() const noexcept;

private:
    const core::u8 *_bytes{nullptr};
    core::u32 _size{0u};
    core::u32 _sectionCount{0u};
    core::u32 _skippedSections{0u};

    const FactV1 *_facts{nullptr};
    core::u32 _factCount{0u};
    const SourceV1 *_sources{nullptr};
    core::u32 _sourceCount{0u};
    const AttributionV1 *_attributions{nullptr};
    core::u32 _attributionCount{0u};
    const CandidateV1 *_candidates{nullptr};
    core::u32 _candidateCount{0u};

    const ReliefV1 *_relief{nullptr};       ///< At most one: a world has one ground.
    const core::i16 *_reliefSamples{nullptr};
    const DocumentV1 *_documents{nullptr};
    core::u32 _documentCount{0u};
    const LocusV1 *_loci{nullptr};
    core::u32 _locusCount{0u};

    const VocabularyEntryV1 *_vocabulary{nullptr};
    core::u32 _vocabularyCount{0u};
    const core::u8 *_catalogue{nullptr};
    core::u32 _catalogueCount{0u};
    const core::u8 *_gazetteer{nullptr};
    core::u32 _gazetteerCount{0u};
    const core::u8 *_placeLink{nullptr};
    core::u32 _placeLinkCount{0u};
    const char *_vocabularyText{nullptr};
    core::u32 _vocabularyTextBytes{0u};

    /**
     * @brief Forgets whatever was adopted.
     *
     * Called on every rejection so a failed @ref open cannot leave a half-adopted image
     * behind — the state after a refusal has to be "nothing", not "the previous image
     * plus the sections of the new one that happened to validate".
     */
    void reset() noexcept;
};

/**
 * @brief Folds a byte image without adopting it.
 *
 * For the writer side and for a freshness check, where the question is whether two byte
 * strings are the same and not whether either is a valid image.
 *
 * @param bytes First byte.
 * @param size  How many.
 * @return The signature.
 */
[[nodiscard]] core::u32 foldImage(const core::u8 *bytes, core::u32 size) noexcept;

} // namespace lpl::knowledge

#endif // LPL_LPL_KNOWLEDGE_KNOWLEDGEPACK_HPP
