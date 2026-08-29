/**
 * @file Markdown.hpp
 * @brief Turning the corpus this project actually has into facts.
 *
 * A new file rather than a stretched `Tei.hpp`, and the reason is the same one that
 * separates a charter from a chronicle: TEI is an encoding for edited literary texts, and
 * what this project owns is a pile of dated working documents. The two share a
 * destination — a `.lplknow` image — and nothing else about how they are read.
 *
 * **Why this corpus first, ahead of the ones the plan names.** It is the only one within
 * reach that satisfies all four conditions at once: it exists, it is the author's own so
 * no licence constrains it, it is small enough that a whole image fits in a byte array a
 * kernel can carry, and it has a consumer on day one — every session re-reads these
 * documents to find what was already decided. An index over OpenAlex would satisfy none of
 * those yet.
 *
 * **What is extracted, and what is deliberately not.** The reading is STRUCTURAL —
 * identifiers, where each is defined, who cites it, the line that defines it, and the dates
 * on the headings — and never semantic. Pulling CLAIMS out of prose needs a model; that is
 * the inference side's job, and guessing at it with pattern matching would produce a corpus
 * whose confidences mean nothing.
 *
 * **The point is to take the maintenance away from the hand that does it now.** A document
 * like `EXTRACTION.md` is a fact table kept by a person: hundreds of identifiers, each with
 * a definition and a file-and-line reference, all of it drifting the moment anything moves.
 * The split that makes it tractable is between the GRAPH and the TEXT:
 *
 *   - the **graph** — which identifiers exist, where each is defined, who cites it, whether
 *     every citation resolves — is derived here, and is therefore never wrong for long;
 *   - the **text** — what each item actually says — is authored, and is carried verbatim in
 *     the image's `Texts` section so it survives with its identifier.
 *
 * So an index stops being a table somebody edits and becomes a VIEW that is rendered: edit
 * the document, re-ingest, re-render. Measured on the real corpus the day this was written:
 * 711 documents, 533 identifiers, 698 citations, **zero broken references** and exactly one
 * identifier defined twice — a finding no reader had ever produced by hand.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_MARKDOWN_HPP
#    define LPL_LPL_HARVEST_MARKDOWN_HPP

#    include <lpl/Foundation.hpp>
#    include <lpl/corpus/Locus.hpp>
#    include <lpl/harvest/Baker.hpp>

#    include <string>
#    include <string_view>
#    include <vector>

namespace lpl::harvest {

/**
 * The predicates a markdown reading can assert.
 *
 * A closed set, and small on purpose: every one of them is something the TEXT states
 * structurally, so none of them needs a judgement. The moment a predicate would need one —
 * "supersedes", "contradicts" — it belongs to a reader that can read, not to a scanner.
 *
 * @warning **Neither of these is FUNCTIONAL, and that has a consequence worth naming.**
 * `history::contradicts` implements the mutual-exclusion rule — same subject, same
 * predicate, overlapping windows, different objects cannot both hold — and its own header
 * says why: *a person is not in two places in the same year*. That assumes one object per
 * subject per window. An identifier is legitimately cited in twenty documents, so a corpus
 * of citations makes that rule fire on every pair, and it means nothing when it does.
 *
 * Measured on the real corpus: 1211 facts produced **226 flagged pairs and 0 demotions** —
 * the rule finding "contradictions" that are simply a well-cited identifier.
 *
 * So a document corpus is for **retrieval**, not for adjudication. A consensus computed
 * over it is not wrong so much as meaningless, and the tools say so rather than printing a
 * number that reads like a verdict. Adjudication needs functional predicates, which is what
 * `history::parityCorpus` has and what a historical corpus will have.
 */
enum : core::u32 {
    kPredicateDefinedIn = 1001u, ///< This identifier has its definition here. NOT functional.
    kPredicateCitedIn = 1002u,   ///< This identifier is referred to here. NOT functional.
    /**
     * The verbatim words of this identifier's definition are line N of the Texts section.
     *
     * The predicate that turns an index from a table somebody edits into a view something
     * renders. A definition's TEXT cannot be derived — a person or a model wrote it — so it
     * travels with its identifier instead, and the index is regenerated rather than
     * maintained.
     */
    kPredicateDefinitionText = 1003u,
};

/**
 * @struct MarkdownSource
 * @brief One document to read.
 */
struct MarkdownSource {
    std::string path;      ///< Where it is on disk.
    std::string canonical; ///< How it should be cited — a path relative to the corpus root.
};

/**
 * @struct IngestReport
 * @brief What reading a corpus found, and what it could not stand behind.
 */
struct IngestReport {
    core::u32 documents{0u};   ///< Documents read.
    core::u32 lines{0u};       ///< Lines scanned.
    core::u32 headings{0u};    ///< Headings seen.
    core::u32 definitions{0u}; ///< Identifiers with a definition.
    core::u32 citations{0u};   ///< References to an identifier from somewhere else.
    core::u32 dangling{0u};    ///< References to an identifier of a REAL scheme that nothing defines.
    core::u32 unknownScheme{0u}; ///< Tokens of identifier shape whose prefix no scheme uses. Noise.
    core::u32 duplicates{0u};  ///< Identifiers defined in two places. See below.
    core::u32 dated{0u};       ///< Headings carrying a date.
    core::u32 footnotes{0u};   ///< Footnotes defined. See @ref kPredicateDefinedIn.
    core::u32 footnoteCitations{0u}; ///< References to a footnote from the prose.
    core::u32 danglingFootnotes{0u}; ///< References to a note nothing defines in that chapter.
    /**
     * Footnote markers in documents that define no notes at all. Noise, not defects.
     *
     * The @ref looksLikeIdentifier scheme rule, one level up: shape alone cannot tell a
     * reference from prose quoting the notation, and only the corpus can. Measured on this
     * project — `CLAUDE.md` writes "notes [^19] [^20]" while discussing the book, and reading
     * those two as broken references is what buries the ones that are real.
     */
    core::u32 footnoteNoise{0u};
    std::string firstDangling; ///< The first unresolved identifier, for the message.
    std::string firstDuplicate; ///< The first identifier defined twice, for the message.
};

/**
 * @brief Reads a set of markdown documents into a baker.
 *
 * Structural only: identifiers, their definitions, their citations, and the dates on the
 * headings that contain them.
 *
 * @param sources  What to read.
 * @param baker    Where to put it.
 * @param outReport Receives the tally.
 * @return false when a document could not be opened — a corpus that silently omits a file
 *         it was asked to read is a corpus whose counts mean nothing.
 */
[[nodiscard]] bool ingestMarkdown(const std::vector<MarkdownSource> &sources, Baker &baker,
                                  IngestReport &outReport);

/**
 * @brief Is this token shaped like a stable identifier?
 *
 * Two to four upper-case characters with at least one letter, a hyphen, then EXACTLY three
 * digits: `SIM-016`, `DWG-014`, `CT3-004`, `KN-007`.
 *
 * @warning Shape alone is not enough and cannot be made enough — that was measured, not feared.
 * A first version allowed one to four digits and dutifully reported `FNV-1` as an
 * identifier cited 93 times and defined nowhere. Three digits kills `FNV-1a` and `UTF-8`;
 * it does NOT kill `SHA-256`, and no rule written in advance will, because `SHA-256` and
 * `SIM-016` are the same shape.
 *
 * So this predicate only proposes. What DISPOSES is @ref ingestMarkdown, which learns the
 * schemes from the corpus: a prefix is a real identifier scheme when some document defines
 * a member of it. A token whose prefix nothing defines is not a broken reference, it is not
 * an identifier at all — and conflating the two is what buries the handful of references
 * that really are broken.
 *
 * @param text  First byte of the token.
 * @param bytes Its length.
 * @return true when it has the shape.
 */
[[nodiscard]] bool looksLikeIdentifier(const char *text, core::u32 bytes) noexcept;

/**
 * @brief Extracts an ISO date from a line, if it carries one.
 *
 * `YYYY-MM-DD` only. The dated headings of this corpus are written that way, and accepting
 * more formats means guessing between a day and a month for anything ambiguous — a guess
 * that would end up in a fact's validity window, where it decides which of two claims a
 * timeline believes.
 *
 * @param text     First byte of the line.
 * @param bytes    Its length.
 * @param outYear  Receives the year.
 * @return false when the line carries no ISO date.
 */
[[nodiscard]] bool extractIsoYear(const char *text, core::u32 bytes, core::i32 &outYear) noexcept;

/**
 * @brief Builds the corpus-wide name of a footnote.
 *
 * @warning **A footnote number is scoped to its CHAPTER, and getting that wrong fuses notes
 * silently.** Measured on this project's own book before a line of this was written: 95
 * definition lines carrying only **20 distinct numbers**, because the numbering restarts at
 * every chapter. Keyed on the document, those 95 notes would have collapsed onto 20
 * identifiers — 75 of them reported as defined twice, and every one of the 119 references
 * resolving to whichever note happened to win. Keyed on the chapter: 95 pairs, zero
 * duplicates, and all 119 references resolving inside their own chapter.
 *
 * The same shape as the `[S<id>]` tags in a research report, which are scoped to one RUN, and
 * as `corpus::workIdentifier` refusing to let a passage into a work's identity. A locally
 * meaningful number is not an identity until something says which local.
 *
 * @param canonical How the document is cited.
 * @param chapter   Ordinal of the enclosing top-level heading, counting from one.
 * @param number    The footnote's number as written.
 * @return A name unique across the corpus, e.g. `docs/Book.md#ch7[^19]`.
 */
[[nodiscard]] std::string footnoteName(std::string_view canonical, core::u32 chapter, std::string_view number);

} // namespace lpl::harvest

#endif // LPL_LPL_HARVEST_MARKDOWN_HPP
