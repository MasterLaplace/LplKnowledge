/**
 * @file Tei.hpp
 * @brief Parsing TEI-XML corpora into facts and texts.
 *
 * The classical and medieval corpora are already structured and already free.
 * They are the correct place to start, not the hardest one.
 *
 * **And they are what `corpus/` was built for, which has been true since before anything
 * could prove it.** `Urn.hpp` parses CTS URNs and its own example is
 * `urn:cts:greekLit:tlg0016.tlg001.perseus-grc2` — Herodotus at Perseus. `Locus` carries
 * three ordinals named part, section and line. Until this file existed, every producer in the
 * repository called `lineLocus`, so the part level was never once written: the addressing the
 * module was designed around had no author. This is that author.
 *
 * **Measured on the real corpora before a line of this was written**, because the shape had
 * to be observed rather than assumed:
 *
 *   - Herodotus (`tlg0016.tlg001.perseus-grc2`, 2.9 MB) declares `refsDecl n="CTS"` with
 *     three levels — book, chapter, section — and holds 9 books, 1578 chapters, 4338
 *     sections. Caesar (`phi0448.phi001.perseus-lat2`) declares the same three and holds
 *     8 / 404 / 2150. The scheme is not one file's habit.
 *   - The hierarchy maps onto @ref corpus::Locus exactly: book to `part`, chapter to
 *     `section`, section to `line`.
 *
 * @warning **Two things measured that would each have shipped as a silent defect:**
 *
 *  1. **Attribute order is not significant in XML.** Herodotus writes
 *     `<div n="urn:..." type="edition">` and Caesar writes
 *     `<div type="edition"  xml:lang="lat" n="urn:...">`. A scan that expects `n` before
 *     `type` finds no identity in Caesar and says so quietly, which reads exactly like a file
 *     that has none. Attributes are parsed as a set here, never as a sequence.
 *  2. **Ordinals are not always numeric.** Herodotus has 45 chapters numbered `10A` through
 *     `10H` — editions that were subdivided after their numbering was fixed. `parsePassage`
 *     already decided what to do about that and its comment says so: *a citation range or a
 *     non-numeric reference; not this scheme* — refused, never truncated. So a passage whose
 *     citation this scheme cannot express gets **no locus** and is cited at the level of the
 *     work, and is COUNTED. Mapping `10A` onto some number would render later as a citation
 *     that is not the one in the book, and a wrong citation is worse than an absent one.
 *
 * @warning **Text is opt-in.** The stated goal is a catalogue of everything rather than a copy of
 * it: the corpus is petabytes, its catalogue is tens of gigabytes. Carrying the words is
 * therefore a decision a caller makes per run, not a default — see @ref TeiOptions::carryText.
 * The structure, the identity and the citation scheme are always read, because those are what
 * an index IS.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_TEI_HPP
#    define LPL_LPL_HARVEST_TEI_HPP

#    include <lpl/Foundation.hpp>
#    include <lpl/corpus/Language.hpp>
#    include <lpl/corpus/Locus.hpp>
#    include <lpl/harvest/Baker.hpp>

#    include <string>
#    include <string_view>
#    include <vector>

namespace lpl::harvest {

class AuthorDates;

/**
 * The predicates a TEI reading can assert.
 *
 * Numbered apart from the markdown reader's 1001–1003 and the research reader's 1101–1106,
 * because an identifier travels verbatim into every image and a predicate that changed value
 * would silently reinterpret every `.lplknow` already written.
 *
 * @warning These are claims about a TEXT, not about the world. `attributed-to` says the edition
 * names Herodotus as its author, which is a fact about the document and is certain; whether
 * Herodotus wrote it is a historical question, belongs to `history::`, and is not asserted
 * here at any confidence.
 */
enum : core::u32 {
    kPredicateAttributedTo = 1201u,  ///< This work's edition names this author. NOT functional.
    kPredicateWrittenIn = 1202u,     ///< This work is in this language. Functional.
    kPredicateWorkTitle = 1203u,     ///< The work's title, verbatim; line N of Texts.
    /**
     * 1204 is RETIRED and deliberately not reused.
     *
     * It said "this passage belongs to this work", which required minting an identifier per
     * passage — and that is what broke: `nameIdentifier` is 32 bits, Perseus carries 395 470
     * passages, and two collided within seconds of the first full run. A passage is a POSITION,
     * which `FactV1::locus` already expressed; the predicate was asserting what the record
     * structurally said. The number stays burned because an identifier travels verbatim into
     * every image, and reusing it would reinterpret any already written.
     */
    kPredicatePassageText = 1205u,   ///< The passage's words, verbatim; line N of Texts.
    kPredicateEditedBy = 1206u,      ///< The modern editor this edition names. NOT functional.
    kPredicateCitationScheme = 1207u, ///< The levels this work is cited by; line N of Texts.

    /**
     * This work names this place, here. NOT functional.
     *
     * @warning A fact about the TEXT, deliberately, and the name says so. "Herodotus names Aetna at
     * 1.2" is observable by opening the passage; "Aetna existed in the fifth century" is an
     * inference somebody has to argue for. A predicate called anything like `located-in` would
     * quietly promote the first into the second across every consumer downstream.
     */
    kPredicateNamesPlace = 1208u,

    /// This work names this person, here. NOT functional, for the same reason as 1208.
    kPredicateNamesPerson = 1209u,

    /**
     * This passage carries a date the editor marked up; the window is the fact's own.
     *
     * @warning The object is the WORK and the window carries the date, rather than the date being an
     * object of its own. A date is not an entity to be named, it is when something was -- which
     * is exactly what `Fact::fromDay`/`toDay` are, and minting identifiers for dates would repeat
     * the mistake that retired 1204.
     */
    kPredicateDatedHere = 1210u,
};

/**
 * @struct TeiSource
 * @brief One TEI document to read.
 */
struct TeiSource {
    std::string path;      ///< Where it is on disk.
    std::string canonical; ///< How it should be cited when the file names no CTS URN.
};

/**
 * @struct TeiOptions
 * @brief What a run of the reader is for.
 */
struct TeiOptions {
    /**
     * Carry the words of every passage into the image.
     *
     * Off by default. An index of the world's texts is affordable precisely because it is an
     * index: the catalogue of everything is tens of gigabytes while the corpus is petabytes.
     * A caller that wants a readable cartridge of one author turns it on knowing what it
     * costs.
     */
    bool carryText{false};

    /**
     * Longest passage carried, in bytes; longer ones are cut and counted.
     *
     * A bound rather than a trust: the input is a harvested file whose structure this reader
     * did not choose, and one malformed `<div>` swallowing a whole book must cost a bounded
     * amount of memory rather than however much the file asks for.
     */
    core::u32 maxPassageBytes{4096u};

    /**
     * Record what each passage NAMES: places, people, and the dates an editor marked up.
     *
     * Off by default, like @ref carryText and for a related reason -- it is a second index over
     * the same walk, and a caller indexing a catalogue does not want it. Measured on Perseus:
     * 40 449 place mentions, 13 564 person mentions, 12 644 dates.
     *
     * @warning Emitted only from LEAF textparts, exactly as the words are. A container's span
     * includes its children's, so scanning every level would record each mention once per
     * ancestor -- a chapter, its book and the work all "naming" the same place, which reads as
     * corroboration and is one editor's single tag.
     */
    bool carryMentions{false};

    /**
     * Where to look an author's dates up; null when there is nowhere to look.
     *
     * @warning **A join across catalogues that share no identifier, so it fills a value and never an
     * identity.** An exactly matching name gives the edition a composition window and sets
     * `kSourceFlagWindowFromNameMatch`, so a caller can keep or drop every soft-joined record. A
     * merely similar name fills nothing and records a `SectionType::Candidate` -- the suspicion
     * survives, and somebody can settle it, without two people quietly becoming one.
     */
    const AuthorDates *authorDates{nullptr};

    /**
     * Resemblance below which a near-miss is not even proposed, raw Q16.16; 0.55 by default.
     *
     * A reporting threshold, never a merging one: getting it wrong costs attention rather than
     * correctness, which is the only kind of threshold this reader is willing to have.
     */
    core::u32 proposeThreshold{36045u};
};

/**
 * @struct TeiIngestReport
 * @brief What reading a TEI corpus found, and what it could not address.
 */
struct TeiIngestReport {
    core::u32 documents{0u};  ///< Files read.
    core::u32 works{0u};      ///< Works identified.
    core::u32 passages{0u};   ///< Addressable passages.
    core::u32 unaddressable{0u}; ///< Passages whose citation this scheme cannot express. See below.
    core::u32 deepest{0u};    ///< Deepest textpart nesting seen, whether or not it fits a Locus.
    /**
     * Passages nested deeper than a `Locus` can hold.
     *
     * @warning A real limit of the format, not of this reader: `corpus::Locus` is three ordinals and
     * `parsePassage` refuses a fourth rather than truncating. Measured on Perseus — 3 works of
     * 2242 nest four textparts deep. They are read and counted; their innermost divisions are
     * cited at the level their citation can express.
     */
    core::u32 tooDeep{0u};
    /// Textparts carrying no `@n` at all, so nothing names them. See @ref unaddressable.
    core::u32 unnumbered{0u};
    core::u32 textLines{0u};  ///< Passages whose words were carried.
    core::u32 truncated{0u};  ///< Passages cut at @ref TeiOptions::maxPassageBytes.
    core::u32 withoutUrn{0u}; ///< Files naming no CTS URN, identified by their path instead.
    core::u32 authors{0u};    ///< Distinct authors named.
    std::string firstUnaddressable; ///< The first citation that could not be expressed.
    /**
     * The identity two works collided on, and the file that lost.
     *
     * @warning A refusal that does not name what it refused sends the reader to guess, and guessing
     * is what this repository keeps paying for. Measured: the first run over the whole Perseus
     * corpus refused, and the message said only that something had collided.
     */
    std::string firstCollision;

    core::u32 placeMentions{0u};  ///< `<placeName>` recorded, with or without an authority key.
    core::u32 personMentions{0u}; ///< `<persName>` recorded.
    core::u32 datedPassages{0u};  ///< `<date>` elements whose window could be read.
    /**
     * Place mentions carrying no authority key.
     *
     * @warning Counted apart because they are a different KIND of record, not a failure. A keyed
     * mention is an editor stating which entry of an authority a word denotes -- a hard key, which
     * may fuse. An unkeyed one is a word, and "Alexandria" names ten places; matching on it would
     * put the library in Afghanistan. Measured on Perseus: 1601 of 40 449.
     */
    core::u32 unkeyedPlaces{0u};

    /// Editions whose composition window came from an exact author-name match.
    core::u32 datedByNameMatch{0u};
    /// Editions whose author only RESEMBLED one, so a candidate was recorded and nothing filled.
    core::u32 proposedByName{0u};
};

/**
 * @brief Reads a set of TEI documents into a baker.
 *
 * @param sources What to read.
 * @param options What the run is for.
 * @param baker   Where to put it.
 * @param outReport Receives the tally.
 * @return false when a document could not be opened, or when two works collide on one
 *         identifier — a corpus that silently merges two works is worse than one that refuses.
 */
[[nodiscard]] bool ingestTei(const std::vector<TeiSource> &sources, const TeiOptions &options, Baker &baker,
                             TeiIngestReport &outReport);

/**
 * @brief Does this document look like TEI?
 *
 * Keyed on the TEI namespace declaration rather than on the root tag alone: `<TEI>` is a
 * plausible element name in other schemas, and the namespace is what the standard actually
 * pins down.
 *
 * @param head The first bytes of the file.
 * @return true when a TEI reader should handle it.
 */
[[nodiscard]] bool looksLikeTei(std::string_view head) noexcept;

/**
 * @brief Maps a TEI `xml:lang` code onto the corpus language tag.
 *
 * @param code The code, e.g. "grc" or "lat".
 * @return The tag; `Unknown` for anything not in the enumeration.
 */
[[nodiscard]] corpus::LanguageTag teiLanguage(std::string_view code) noexcept;

} // namespace lpl::harvest

#endif // LPL_LPL_HARVEST_TEI_HPP
