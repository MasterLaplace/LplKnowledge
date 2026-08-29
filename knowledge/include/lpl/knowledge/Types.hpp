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
    /**
     * Works that exist somewhere, without their text (see CatalogueEntryV1).
     *
     * The section that makes an index of everything affordable. Measured on the real corpora:
     * Perseus is 2242 works whose structure costs 7 MB and whose words cost 257 MB; Internet
     * Archive holds 51.6 million texts and HathiTrust's whole catalogue is 1.22 GB compressed.
     * The corpus is petabytes, the catalogue is tens of gigabytes, and this is where the
     * second one lives.
     */
    Catalogue = 8u,

    /**
     * Where named places are, and when they were there (see GazetteerEntryV1).
     *
     * @warning The section that lets a claim about a person become a position in a world. Kept apart
     * from Catalogue because the two answer different questions about different things: a
     * holding says a BOOK exists somewhere on a shelf, a gazetteer entry says a PLACE existed
     * somewhere on the earth, between two years.
     */
    Gazetteer = 9u,

    /**
     * Which named places the sources say were connected (see PlaceLinkV1).
     *
     * @warning A section rather than a field, because the degree is wildly uneven: measured on
     * Pleiades, the median place has ONE link and the busiest has twenty-five. A fixed array in
     * the entry would waste twenty-four slots on almost every row to serve a handful.
     */
    PlaceLink = 10u,

    /**
     * Pairs that MIGHT be one thing, with how strongly and on what evidence.
     *
     * @warning **The suspicion, kept rather than acted on.** `harvest::Clustering::candidates` says
     * it in its own comment -- "alike, not merged" -- and until this section existed it was
     * computed and thrown away at the end of every run. That loses the one thing worth keeping:
     * splitting two records is reversible, merging them is not, so the safe default is to leave
     * them apart -- but leaving them apart AND forgetting they resembled each other means
     * corroboration can never happen, and one man with forty sources stays forty men with one.
     *
     * A candidate is never a claim that two things are one. It is a claim that somebody should
     * look, which is exactly the question a specialist can answer and a hard key can settle.
     */
    Candidate = 11u,

    /**
     * The credits an image must carry to be redistributable at all.
     *
     * @warning **Permission to pass this on is CONDITIONAL, so the condition travels with the bytes.**
     * Measured on the corpora actually ingested: 1344 of the Perseus TEI files declare
     * CC-BY-SA 4.0 in their own header, Pleiades is CC-BY 3.0, and the elevation tiles are public
     * domain *provided* the surveys are credited. An image that carries the samples and leaves the
     * credit in a README beside it is an image nobody may lawfully forward -- and a README is
     * exactly the thing that gets separated from a file.
     *
     * @warning It records, it does not judge. Share-alike terms mixed with others may or may not be
     * compatible, and that is a question for a person; what this guarantees is that the person can
     * see what is in there without re-reading the sources.
     */
    Attribution = 12u,

    /**
     * Measured ground, resampled onto world cells.
     *
     * @warning **A survey is a corpus, which is why it lives here and not in a cartridge.** These
     * samples are somebody's measurement of the earth, they carry a licence, and that licence is
     * carried by @ref Attribution in this same image. Splitting the samples from the credit across
     * two formats is precisely the arrangement that makes an image nobody may redistribute.
     *
     * @warning **Already in CELL space: the resampling happens once, at bake time, and never in
     * ring 0.** An arc-second is about 30.87 m at the equator and shorter east-west everywhere
     * else, so it never lines up with a thirty-metre cell and something must resample. Doing it on
     * the reading side would put a resampler in ring 0 and, worse, make it the SECOND one -- two
     * answers to what the ground is at a cell.
     */
    Relief = 13u,
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
    /**
     * First day the claim covers, in the epoch @c lpl::history::Calendar defines.
     *
     * @warning DAYS, and the change was a real repair rather than a widening for its own sake. As
     * years, a source that knew a date to the day had nowhere to put it -- so a ship's log and a
     * legend went onto the wire as the same claim, and no consumer downstream could tell them
     * apart. The precision now lives in the WIDTH of the interval: a year-resolution source
     * writes the whole year, a precise one writes one day.
     */
    core::i32 fromDay;

    /// Last day it covers; equal to @c fromDay for an instant.
    core::i32 toDay;
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
    core::u32 id;   ///< Matches FactV1::source.
    core::u32 kind; ///< A @ref SourceKindV1 value.

    /**
     * Distance between the event and the writing, or @c lpl::history::kUnknownYearsAfterEvent.
     *
     * @warning **Set directly only when nothing better is known.** The distance belongs to the PAIR
     * (source, claim), not to the source: a chronicle compiled in 1200 recounting both 1190 and
     * 400 is ten years from one claim and eight hundred from the other, and one number cannot be
     * right about both. Prefer @ref composedFrom, from which the distance is derived per claim.
     */
    core::u32 yearsAfterEvent;

    core::u32 agreements; ///< Independent sources that concur.
    core::u32 name;       ///< Identifier whose vocabulary entry names it.
    core::u32 document;   ///< Index into Documents, or @ref kNoIdentifier.

    /**
     * First day the source could have been written; 0 when unknown.
     *
     * @warning An INTERVAL, because that is what is known. For a work whose author is dated, the
     * bounds are hard rather than estimated -- a text cannot be written before its author was
     * born nor after they died -- and the window is as wide as that life. Herodotus comes out
     * fifty-four years wide, which is not imprecision, it is the truth about what anybody knows.
     * Measured: 65 933 of 79 179 Gutenberg records carry both bounds.
     */
    core::i32 composedFrom;

    /// Last day it could have been written; 0 when unknown.
    core::i32 composedTo;

    /**
     * kSourceFlag* bits: how much this record's own claims are worth believing.
     *
     * @warning **What makes an uncertain value acceptable rather than corrosive.** A window derived
     * by matching an author's NAME across two catalogues is a guess about identity, and a guess
     * is admissible here only because it is marked: a caller that wants the factual half can drop
     * every soft-joined record, and one that wants a full world can keep them. What must never
     * happen is the same guess FUSING two people -- no filter separates them afterwards, and the
     * corpus is then not noisy but confidently wrong.
     */
    core::u32 flags;
};
// 24 -> 32 when the composition window arrived. The assertion means "this layout is deliberate",
// never "it will not change": it catches padding a compiler slipped in, which is silent, and it
// caught this growth, which is not.
static_assert(sizeof(SourceV1) == 36u, "SourceV1 layout is wire format");

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
 * @struct CatalogueEntryV1
 * @brief Wire form of a work that exists somewhere, and where to get it.
 *
 * A catalogue row is the GENERATOR of a text rather than the text — which is the thread the
 * whole project already pulls on ("never store the result, always the generator"). What it
 * holds is the minimum that lets something later decide whether to fetch: what it is called,
 * who made it, when, who holds it, and the address.
 *
 * @warning **Every string here is a Texts LINE INDEX, never a hashed identifier**, and that is the
 * one decision this record exists to get right. `corpus::nameIdentifier` is 32 bits, so
 * hashing names collides at scale — measured, not feared: 395 470 passage names produced a
 * collision between two unrelated works within seconds. A catalogue is millions of rows, where
 * hashing would not collide occasionally but constantly. An index into a table cannot collide
 * with anything; it is a position, and positions are exact.
 *
 * @warning @c cluster is the one identifier-shaped field, and it is filled by
 * `harvest::EntityResolution` AFTER the whole batch is readable — because whether two rows are
 * one work is a fact about the batch, not about either row. Zero means "not resolved", which is
 * different from "resolved to be alone" and is left different on purpose.
 */
struct CatalogueEntryV1 {
    /**
     * ONE-BASED Texts line holding the title, or @ref kNoIdentifier when there is none.
     *
     * @warning One-based for the reason `Baker::addDocument` already gives about its own indices:
     * zero has to keep meaning "none". Measured the hard way — with zero-based indices, a row
     * whose creator the holder does not name kept a zero, and zero is a perfectly good line, so
     * `lpl-ask` rendered ANOTHER row's text as the author. A US Geological Survey serial came
     * back credited to a Japanese journal, and nothing about the output looked wrong.
     *
     * @warning And the test that should have caught it could not: it asserted `creator == kNoIdentifier`,
     * which is zero, which is exactly what the field held. Absence and line zero were the same
     * value, so no assertion over that value could tell them apart.
     */
    core::u32 title;
    core::u32 creator;  ///< One-based Texts line for the author as the holder writes it, or none.
    core::u32 address;  ///< One-based Texts line for where to get it, or none.
    core::u32 holder;   ///< Identifier of the repository; few enough that hashing is safe.
    /**
     * First year the work could be from; 0 when unknown.
     *
     * @warning **A WINDOW, and the old single field admitted its own problem in its own comment:
     * "year of the edition OR of composition".** Two different facts in one integer. For an
     * edition the two ends coincide and the window is one year wide, which is correct. For a work
     * dated only through its author they do not: Herodotus came out as "-430", his death year,
     * when what is known is that he wrote somewhere in [-484, -430]. Fifty-four years collapsed
     * to a point, and the point was the end of it.
     */
    core::i32 year;

    /// Last year it could be from; equal to @ref year for an edition, 0 when unknown.
    core::i32 yearTo;
    core::u32 language; ///< A `corpus::LanguageTag` value.
    core::u32 flags;    ///< kCatalogueFlag* bits.
    core::u32 cluster;  ///< Which work this row is a copy of, once resolved. See above.
};
static_assert(sizeof(CatalogueEntryV1) == 36u, "CatalogueEntryV1 layout is wire format");

/**
 * @struct GazetteerEntryV1
 * @brief Wire form of a named place: where, and between which years.
 *
 * @warning **The coordinates are AUTHORITATIVE, and therefore raw Q16.16 words rather than floats.**
 * Where a place is decides where a body walks and how long it takes to get there, so two targets
 * disagreeing in the last bit would run two different journeys from one corpus. Degrees fit the
 * format with room to spare — ±180 against a ±32767 range — and the resolution is 1/65536 of a
 * degree, about 1.7 metres, which is finer than any ancient site is known to.
 *
 * @warning **`place` is the repository's own identifier, never a hash of the name.** Measured on
 * Pleiades the moment it was opened: TEN distinct places are called "Alexandria", from Egypt to
 * Afghanistan, and several are called "Athenae" — one of them unlocated. Matching by title puts
 * the Library of Alexandria in Kabul. This is the same rule `harvest::EntityResolution` states:
 * a hard key merges, a score only proposes.
 */
struct GazetteerEntryV1 {
    core::u32 place;   ///< The gazetteer's own stable identifier. The hard key.
    core::u32 title;   ///< ONE-BASED Texts line naming it, or kNoIdentifier. Zero must stay "none".
    core::i32 latRaw;  ///< Latitude in degrees, as a raw Q16.16 word.
    core::i32 lonRaw;  ///< Longitude in degrees, as a raw Q16.16 word.
    core::i32 minYear; ///< First year the place is attested. See kGazetteerFlagDated.
    core::i32 maxYear; ///< Last year it is attested.
    core::u32 flags;   ///< kGazetteerFlag* bits.
    core::u32 kinds;   ///< kPlaceKind* bits: what sort of place it is.
};
static_assert(sizeof(GazetteerEntryV1) == 32u, "GazetteerEntryV1 layout is wire format");

/**
 * The entry carries coordinates.
 *
 * @warning Its absence is a FACT rather than a gap: 6 502 of Pleiades' 42 400 places are marked
 * unlocated — known from texts, never found on the ground. They belong in the corpus, and giving
 * them invented coordinates would turn "we do not know where Cimmeria was" into a map pin.
 */
inline constexpr core::u32 kGazetteerFlagLocated = 1u << 0;

/// The repository calls the position precise rather than approximate.
inline constexpr core::u32 kGazetteerFlagPrecise = 1u << 1;

/// The entry carries a temporal window. Absent means the place is undated, not eternal.
inline constexpr core::u32 kGazetteerFlagDated = 1u << 2;

/// A place people lived in.
inline constexpr core::u32 kPlaceKindSettlement = 1u << 0;
/// A settlement the repository calls urban.
inline constexpr core::u32 kPlaceKindUrban = 1u << 1;
/// Water: a river, a lake, a spring.
inline constexpr core::u32 kPlaceKindWater = 1u << 2;
/// Military: a fort, a camp, a wall.
inline constexpr core::u32 kPlaceKindFortification = 1u << 3;
/// Sacred: a temple, a sanctuary, a shrine.
inline constexpr core::u32 kPlaceKindSacred = 1u << 4;
/// A road, a pass, a station along one.
inline constexpr core::u32 kPlaceKindRoute = 1u << 5;
/// Terrain: a mountain, an island, a cape.
inline constexpr core::u32 kPlaceKindLandform = 1u << 6;

/**
 * @struct PlaceLinkV1
 * @brief One attested connection between two named places.
 *
 * @warning **This is the "facts first" half of how a body decides where to go.** A corpus states which
 * places were connected — a road, a route, a sea lane somebody actually sailed — and a simulation
 * has no business inventing that. What it may invent is where the road RUNS across the relief,
 * which is `procgen::routeLeastCost`'s job and not this record's.
 *
 * @warning Stored in BOTH directions. The source data is directed — A lists B and B may not list A —
 * but a road is walkable either way, and a reader that had to check two orderings would sooner
 * or later check only one. Measured cost of the symmetry: 15 379 declared links become about
 * thirty thousand pairs, which is a quarter of a megabyte.
 */
struct PlaceLinkV1 {
    core::u32 from; ///< A place identifier, as @ref GazetteerEntryV1::place.
    core::u32 to;   ///< The place it connects to.
};
static_assert(sizeof(PlaceLinkV1) == 8u, "PlaceLinkV1 layout is wire format");

/// A scholarly transcription: the words, encoded. What TEI corpora hold.
/**
 * The composition window came from matching an author's NAME, not from a hard key.
 *
 * @warning **The bit that lets uncertainty in without letting it corrupt.** Two catalogues rarely
 * share an identifier for a person, so joining "Herodotus" in a text corpus to "Herodotus" in a
 * bibliographic one is a guess -- a good one, and still a guess. Marked, it is a date window a
 * caller can weigh or discard. Unmarked, it is indistinguishable from a window an editor stated,
 * and the difference between those two is the whole of what provenance means.
 *
 * @warning It never licenses a MERGE. A soft join may fill a value; it may not decide that two
 * records are one person, because that is the one operation no later filter can undo.
 */
/**
 * @struct CandidateV1
 * @brief Wire form of a pair somebody should look at.
 */
struct CandidateV1 {
    core::u32 left;     ///< A mention identifier.
    core::u32 right;    ///< The other, always the larger, so a pair has one spelling.
    core::u32 scoreRaw; ///< Resemblance, raw Q16.16. A word, never a float.
    core::u32 evidence; ///< A `harvest::Evidence` value: what suggested it.
};
static_assert(sizeof(CandidateV1) == 16u, "CandidateV1 layout is wire format");

/**
 * @struct ReliefV1
 * @brief Header of the relief section: how to read the samples that follow it.
 *
 * @warning **There is at most ONE of these in an image, and that is a statement about the world
 * rather than a limitation.** A world has one ground. Two relief sections would be two answers to
 * how high a cell is, and nothing downstream could choose between them -- the same shape as the
 * defects this repository has already paid for under "two answers to where the sea is". An image
 * carrying a second one is refused rather than resolved by order.
 *
 * @warning **Every field is part of the world's identity, not metadata about it.** The projection is
 * stored here rather than recomputed on the reading side because a reader that derived it would be
 * a SECOND projection: the same samples placed in two different valleys, with nothing to say which
 * run was right.
 *
 * The samples follow the header immediately, `width * height` of them, row-major with the NORTH row
 * first, as native-endian `i16` metres -- byte-swapped once by the harvester, so nothing on the
 * reading side ever sees a big-endian word. @ref kReliefNoSampleWire marks a cell nobody measured.
 */
struct ReliefV1 {
    core::u32 width;          ///< Columns, in world cells.
    core::u32 height;         ///< Rows, in world cells, north first.
    core::i32 originCellX;    ///< World cell of column zero.
    core::i32 originCellZ;    ///< World cell of row zero.
    core::i32 latitudeRaw;    ///< NORTH-west corner latitude, degrees, raw Q16.16.
    core::i32 longitudeRaw;   ///< West edge longitude, degrees, raw Q16.16.
    core::i32 referenceLatitude; ///< Standard parallel the east-west scale is exact at.
    core::u32 metresPerCell;  ///< Ground one cell covers.
    core::i32 unitsPerMetreRaw;  ///< Vertical scale, raw Q16.16.
    core::i32 seaLevelUnitsRaw;  ///< World height elevation zero maps to. THE reconciliation.
    core::u32 blendCells;     ///< Cells over which real ground gives way to invented ground.
    /**
     * Which of this tile's four edges face nothing, and therefore fade.
     *
     * @warning The fade belongs to the border of the SURVEY, never to the border of a tile. A tile
     * that faded on every side rings itself in half-invented ground, so a world made of tiles comes
     * out cross-hatched with a gentle valley at every boundary -- and each one reads as terrain
     * rather than as an error. `math::kReliefEdge*` bits; all four set is a lone survey.
     */
    core::u32 exposedEdges;
};
static_assert(sizeof(ReliefV1) == 48u, "ReliefV1 layout is wire format");

/**
 * Sample value meaning "nobody measured this cell".
 *
 * @warning The same word the source tiles use, carried through rather than translated: a gap in a
 * survey is a fact about the survey, and filling it at bake time would make invented ground
 * indistinguishable from measured ground for everything downstream. Must equal
 * `procgen::kReliefNoSample`, and a static assertion on the bridge says so.
 */
inline constexpr core::i16 kReliefNoSampleWire = -32768;

/**
 * @struct AttributionV1
 * @brief One credit the image carries, and what it covers.
 */
struct AttributionV1 {
    /**
     * ONE-BASED Texts line holding the credit verbatim.
     *
     * @warning Verbatim, never summarised. Every one of these lines is a condition of the permission
     * to redistribute, and a shortened licence is a different licence.
     */
    core::u32 text;

    /**
     * Identifier of what it covers: a work, a repository, a dataset -- or @ref kNoIdentifier when
     * it covers the whole image.
     */
    core::u32 covers;
};
static_assert(sizeof(AttributionV1) == 8u, "AttributionV1 layout is wire format");

inline constexpr core::u32 kSourceFlagWindowFromNameMatch = 1u << 0;

/**
 * Nothing about this source's dating is established.
 *
 * Distinct from carrying no window: this says somebody looked and found nothing, which is a
 * different fact from nobody having looked.
 */
inline constexpr core::u32 kSourceFlagUndated = 1u << 1;

inline constexpr core::u32 kCatalogueFlagTranscription = 1u << 0;

/// Machine-read text over a scan. Words, with an error rate nobody has measured per row.
inline constexpr core::u32 kCatalogueFlagOpticalCharacterRecognition = 1u << 1;

/// Images only. Citable and unreadable by a machine.
inline constexpr core::u32 kCatalogueFlagFacsimile = 1u << 2;

/// Free of rights as far as the harvester could establish. NOT a legal opinion.
inline constexpr core::u32 kCatalogueFlagPublicDomain = 1u << 3;

/// Full view: the holder serves the whole thing, not a snippet.
inline constexpr core::u32 kCatalogueFlagFullView = 1u << 4;

/**
 * @c year is the CREATOR'S DEATH YEAR, not a date of publication.
 *
 * @warning A flag rather than a second field, and the reason is that the question a reader asks —
 * "what is old?" — has one answer per row, while the EVIDENCE behind it differs by source. A
 * holdings record from a library states when an edition was printed. Project Gutenberg states
 * when its transcription was uploaded, which for Herodotus is 2006 — so a catalogue that took
 * that as the year would answer "nothing" to every query about antiquity, correctly and
 * uselessly. What the file does carry is that Herodotus died in -430, and a death year is a
 * genuine upper bound on composition rather than a guess.
 *
 * Declaring the provenance is what keeps that honest: a consumer that needs a publication date
 * can refuse these rows, and one that wants "written before 1900" can take them. Measured on
 * the full Gutenberg catalogue: 61 628 of 79 179 works carry a creator death year, 19 034 of
 * them before 1900.
 */
inline constexpr core::u32 kCatalogueFlagYearFromAuthor = 1u << 5;

/**
 * @struct CatalogueHeaderV1
 * @brief Prologue of the Catalogue section.
 */
struct CatalogueHeaderV1 {
    core::u32 count;    ///< Entries that follow.
    core::u32 reserved; ///< Must be 0.
};
static_assert(sizeof(CatalogueHeaderV1) == 8u, "CatalogueHeaderV1 layout is wire format");

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
