/**
 * @file Types.hpp
 * @brief Fixed-width identifiers shared by every knowledge artefact.
 *
 * Flat, versioned, endian-explicit. These cross the boundary between a host
 * harvester and a ring-0 reader, so nothing here may depend on how a compiler
 * chooses to lay out a bool.
 *
 * Two decisions are worth stating, because they are what let both halves use this
 * file at once.
 *
 * **A confidence travels as a RAW WORD, not as a Fixed32.** `history::Fact` holds a
 * `math::Fixed32` because it does arithmetic on it, and arithmetic is where a rounding
 * difference between two targets turns into two different histories. A wire record does
 * no arithmetic: it carries the Q16.16 word verbatim and hands it over. That is what
 * lets this module compile in a standalone build where `Fixed32` does not exist —
 * `include/lpl/Foundation.hpp` refuses to emulate it, and a format that required it
 * would have made the standalone build a fiction. `pack::RecipeV1` made the same call.
 *
 * **An identifier travels VERBATIM, never re-indexed.** A subject is `1` in the image
 * because it is `1` in the corpus, and the vocabulary is a side table saying that `1`
 * reads "king". The tempting alternative — vocabulary index as identity — would
 * renumber every claim at bake time, so a corpus round-tripped through an image would
 * fold differently from the same corpus held in memory, and the one property this
 * format exists to have would be gone.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_KNOWLEDGE_TYPES_HPP
#    define LPL_LPL_KNOWLEDGE_TYPES_HPP

#    include <lpl/Foundation.hpp>

namespace lpl::knowledge {

/// Bytes of the magic identifier at the head of every image.
inline constexpr core::u32 kMagicSize = 8u;

/// Current wire format version. Bump on ANY layout change in this file.
inline constexpr core::u32 kFormatVersion = 1u;

/**
 * Identifier reserved to mean "none".
 *
 * Zero rather than an all-ones sentinel, because zero is what a zeroed page holds: a
 * record that was never written then reads as absent, instead of as a plausible
 * reference to entity 4294967295.
 */
inline constexpr core::u32 kNoIdentifier = 0u;

/**
 * @enum SectionType
 * @brief What a section carries.
 *
 * Unknown types are SKIPPED, never fatal — the same rule `pack::SectionType` follows and
 * for the same reason: a reader that predates a section must survive an image carrying
 * it, or the format can never grow. It is also how a ring-0 reader declines the verbatim
 * text of a work it has no business decoding.
 */
enum class SectionType : core::u32 {
    Unknown = 0u,
    Vocabulary = 1u, ///< What each identifier reads as, for humans (see VocabularyEntryV1).
    Sources = 2u,    ///< Who asserts things, and what is known of them (see SourceV1).
    Facts = 3u,      ///< The claims themselves, sorted (see FactV1).
    Documents = 4u,  ///< The works a claim can point into (see DocumentV1).
    Loci = 5u,       ///< Where in a document (see LocusV1).
    Texts = 6u,      ///< Verbatim text of the documents, addressed by locus.
    Ecc = 7u,        ///< Transversal parity over the rest of the image.
};

/**
 * @struct Header
 * @brief Fixed 32-byte prologue of every image.
 *
 * Deliberately the same shape as `pack::Header`. One format family means one reader
 * discipline and one set of mistakes already made: magic, version, declared size,
 * content hash, and the parity locator held OUT of the protected span.
 */
struct Header {
    char magic[kMagicSize];  ///< "LPLKNOW\0", never NUL-terminated as a string.
    core::u32 formatVersion; ///< @ref kFormatVersion when written.
    core::u32 totalSize;     ///< Size of the whole image, header included.
    core::u32 sectionCount;  ///< Entries in the section table that follows.
    core::u32 contentHash;   ///< FNV-1a over every byte after this header.

    /**
     * Offset and size of the parity section, or 0 when there is none.
     *
     * Held here and not only in the section table, and that redundancy is the whole
     * reason a repair can work: the parity protects everything after this header,
     * INCLUDING the table, so a burst landing on the table would otherwise destroy the
     * only record of where the parity is. That was measured on `.lplpak` — 30 of 31
     * whole-row bursts repaired, and the one that wiped row zero reported "no parity
     * section" on an image that had one. The lesson is inherited here rather than
     * paid for a second time.
     */
    core::u32 eccOffset;
    core::u32 eccSize;
};
static_assert(sizeof(Header) == 32u, "KnowledgePack header layout is wire format");

/**
 * @struct SectionEntry
 * @brief One row of the section table, immediately after the header.
 */
struct SectionEntry {
    core::u32 type;     ///< A @ref SectionType value.
    core::u32 offset;   ///< Byte offset from the start of the image.
    core::u32 size;     ///< Byte length of the section payload.
    core::u32 reserved; ///< Must be 0.
};
static_assert(sizeof(SectionEntry) == 16u, "KnowledgePack section entry is wire format");

/**
 * @struct FactV1
 * @brief Wire form of one assertion: who says what of whom, when, and how sure.
 *
 * The sextuplet, plus the one thing a sextuplet on its own cannot give: WHERE the claim
 * was found. @c locus is what turns "a chronicle says so" into "line 412 of book 3 says
 * so", which is the difference between a citation and a rumour.
 */
struct FactV1 {
    core::u32 subject;       ///< Who or what the claim is about.
    core::u32 predicate;     ///< What is claimed of it.
    core::u32 object;        ///< The value claimed.
    core::i32 fromYear;      ///< First year the claim covers.
    core::i32 toYear;        ///< Last year it covers; equal to @c fromYear for an instant.
    core::u32 source;        ///< Which source asserted it.
    core::u32 confidenceRaw; ///< Confidence in [0,1] as a raw Q16.16 word.
    core::u32 locus;         ///< Index into the Loci section, or @ref kNoIdentifier.
};
static_assert(sizeof(FactV1) == 32u, "FactV1 layout is wire format");

/**
 * @enum SourceKindV1
 * @brief What kind of thing said it.
 *
 * The values MIRROR `history::SourceKind`, and the ordering is load-bearing in both
 * places: it runs from sources whose interest is to record accurately to sources whose
 * interest is to persuade. Mirrored by value rather than shared as a type, because this
 * file has to compile without LplPlugin on the include path — and asserted equal in
 * `History.hpp`, where both are visible, so a reordering on either side fails a build
 * instead of silently reinterpreting every source in every image already on disk.
 */
enum class SourceKindV1 : core::u32 {
    Notarial = 0u,       ///< Contracts, registers, acts. Written to be checked.
    Archaeology = 1u,    ///< Material evidence. Silent about motive, hard to forge.
    Administrative = 2u, ///< Censuses, tax rolls. Accurate about what was taxed.
    Chronicle = 3u,      ///< A contemporary account. Honest and partial.
    Panegyric = 4u,      ///< Written to praise. Accurate only by accident.
    Count = 5u,
};

/**
 * @struct SourceV1
 * @brief Wire form of what is known about a source.
 */
struct SourceV1 {
    core::u32 id;              ///< Matches FactV1::source.
    core::u32 kind;            ///< A @ref SourceKindV1 value.
    core::u32 yearsAfterEvent; ///< Distance between the event and the writing.
    core::u32 agreements;      ///< Independent sources that concur.
    core::u32 name;            ///< Identifier whose vocabulary entry names it.
    core::u32 document;        ///< Index into Documents, or @ref kNoIdentifier.
};
static_assert(sizeof(SourceV1) == 24u, "SourceV1 layout is wire format");

/// A modern derivative rather than the source: readable, and a copyrighted work.
inline constexpr core::u32 kDocumentFlagTranslation = 1u << 0;

/// Free of rights, as far as the harvester could establish.
inline constexpr core::u32 kDocumentFlagPublicDomain = 1u << 1;

/// Addressed by file and line rather than by book, chapter and line.
inline constexpr core::u32 kDocumentFlagPlainText = 1u << 2;

/**
 * @struct DocumentV1
 * @brief Wire form of a work a claim can point into.
 *
 * "Work" is meant loosely on purpose, and @ref kDocumentFlagPlainText is why: a
 * classical text is addressed by book, chapter and line, and a working document is
 * addressed by file and line. Those are the same problem — name a position inside
 * something immutable — and giving each its own record type would have been two
 * addressing schemes, which is how a citation stops being checkable.
 */
struct DocumentV1 {
    core::u32 urn;      ///< Identifier whose vocabulary entry holds the canonical name.
    core::u32 title;    ///< Identifier whose vocabulary entry holds a human title.
    core::u32 language; ///< A `corpus::LanguageTag` value.
    core::u32 flags;    ///< kDocumentFlag* bits.
};
static_assert(sizeof(DocumentV1) == 16u, "DocumentV1 layout is wire format");

/**
 * @struct LocusV1
 * @brief Wire form of a precise position inside a document.
 *
 * Three ordinals rather than a byte offset, because a byte offset is invalidated by
 * re-encoding a file and a citation has to survive that. For a plain-text document the
 * mapping is stated here and nowhere else: @c part is unused, @c section is the ordinal
 * of the enclosing heading, and @c line is the line number.
 */
struct LocusV1 {
    core::u32 document; ///< Index into the Documents section.
    core::u32 part;     ///< Book, or 0.
    core::u32 section;  ///< Chapter or heading ordinal, or 0.
    core::u32 line;     ///< Line within the section.
};
static_assert(sizeof(LocusV1) == 16u, "LocusV1 layout is wire format");

/**
 * @struct VocabularyEntryV1
 * @brief What one identifier reads as.
 *
 * Sorted by @c id in the baked section so the reader binary-searches instead of
 * scanning: the index was computed once, on the host, which is the whole point of
 * having a writer half.
 */
struct VocabularyEntryV1 {
    core::u32 id;         ///< The identifier this names.
    core::u32 textOffset; ///< Byte offset into the section's text area.
};
static_assert(sizeof(VocabularyEntryV1) == 8u, "VocabularyEntryV1 layout is wire format");

/**
 * @struct VocabularyHeaderV1
 * @brief Prologue of the Vocabulary section.
 */
struct VocabularyHeaderV1 {
    core::u32 count;     ///< Entries that follow.
    core::u32 textBytes; ///< Bytes of the NUL-separated text area after the entries.
};
static_assert(sizeof(VocabularyHeaderV1) == 8u, "VocabularyHeaderV1 layout is wire format");

/**
 * @brief The stable word for a section type.
 *
 * A word and never an index, for the reason the project keeps re-learning: an index means
 * whatever the enumeration was worth the day it was printed. Used by the tools that report
 * what an image contains, and by the audit that says which sections were skipped.
 *
 * @param type The section type; may be a value from a newer writer.
 * @return A short, stable string; "unknown" for anything outside the enumeration.
 */
[[nodiscard]] const char *sectionTypeName(SectionType type) noexcept;

/// FNV-1a offset basis: the project's one folding primitive, everywhere.
inline constexpr core::u32 kFnv1aOffsetBasis = 0x811C9DC5u;

/// FNV-1a prime.
inline constexpr core::u32 kFnv1aPrime = 0x01000193u;

/**
 * @brief Folds one word into a running signature.
 *
 * @param hash Running value, updated in place.
 * @param word Word to absorb.
 */
constexpr void foldWord(core::u32 &hash, core::u32 word) noexcept
{
    hash = (hash ^ word) * kFnv1aPrime;
}

/**
 * @brief Folds a byte range into a running signature, one byte at a time.
 *
 * Byte at a time and not word at a time, deliberately: a word-wise fold depends on
 * endianness, so it would hide exactly the class of cross-target disagreement a
 * signature exists to catch.
 *
 * @param hash  Running value, updated in place.
 * @param bytes First byte.
 * @param count How many.
 */
constexpr void foldBytes(core::u32 &hash, const core::u8 *bytes, core::u32 count) noexcept
{
    for (core::u32 i = 0u; i < count; ++i)
        foldWord(hash, static_cast<core::u32>(bytes[i]));
}

} // namespace lpl::knowledge

#endif // LPL_LPL_KNOWLEDGE_TYPES_HPP
