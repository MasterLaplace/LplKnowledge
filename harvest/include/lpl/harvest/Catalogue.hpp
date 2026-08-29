/**
 * @file Catalogue.hpp
 * @brief Indexing what exists without fetching it.
 *
 * The half of the library that makes the whole of it affordable. Measured rather than
 * estimated, on the day this was written:
 *
 *   - Internet Archive holds **51 638 070** texts, of which **5 549 827** were published
 *     before 1900; HathiTrust holds some eighteen million volumes.
 *   - The CORPUS is petabytes. The CATALOGUE is not: HathiTrust's whole holdings file is
 *     **1.22 GB** compressed, Project Gutenberg's RDF catalogue is **177 MB**, OpenLibrary's
 *     dump is 17 GB. The index of everything fits on a laptop when the thing indexed never
 *     will.
 *   - The same ratio holds at small scale and was measured there too: Perseus is 2242 works
 *     whose structure costs 7 MB and whose words cost 257 MB.
 *
 * That is the project's own stated thread applied to holdings — *never store the result,
 * always the generator*. A catalogue row is the generator of a text: what it is called, who
 * made it, when, who has it, and the address that produces it on demand.
 *
 * @warning **Nothing here hashes a name into an identifier**, and that rule is the reason this
 * module can exist at all. `corpus::nameIdentifier` is 32 bits; a catalogue is millions of
 * rows; and the collision it produces is silent — measured on Perseus, where hashing 395 470
 * passage names collided two unrelated works within seconds of the first full run. Every
 * string a catalogue row carries is a Texts LINE INDEX, which is a position and cannot
 * collide with anything.
 *
 * @warning **A catalogue is not a corpus and must not be read as one.** A row asserts that a holder
 * SAYS it has this thing. It does not assert the work is what the row calls it, that the
 * attribution is right, or that the text behind the address is complete. Those are claims
 * about the world; this is a claim about a holding, and `history::` is where the first kind
 * belongs.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_CATALOGUE_HPP
#    define LPL_LPL_HARVEST_CATALOGUE_HPP

#    include <lpl/Foundation.hpp>
#    include <lpl/harvest/Baker.hpp>

#    include <string>
#    include <string_view>
#    include <vector>

namespace lpl::harvest {

/**
 * @struct CatalogueIngestReport
 * @brief What reading a holdings file found, and what it could not stand behind.
 */
struct CatalogueIngestReport {
    core::u32 rows{0u};          ///< Entries written.
    core::u32 malformed{0u};     ///< Lines that did not have the shape the format promises.
    core::u32 publicDomain{0u};  ///< Rows the holder marks free of rights.
    core::u32 fullView{0u};      ///< Rows the holder serves whole rather than in snippets.
    core::u32 withoutTitle{0u};  ///< Rows naming no title. Counted; a holding with no name is a fact.
    core::u32 withoutYear{0u};   ///< Rows naming no year.
    core::u32 withoutCreator{0u};

    /**
     * Records naming MORE THAN ONE creator, whose date window is the union of their lives.
     *
     * @warning Counted because the window then answers a weaker question than usual: "somebody named
     * on this work could have written it then", rather than "its author could". Measured on
     * Project Gutenberg: 2767 of 79 179, whose death dates are a median 23 years apart -- noise
     * against a proximity term that decays as 1/(1+y/32) -- but a ninetieth percentile of 102
     * years and a widest of 2589, which is an ancient author recorded beside a modern editor.
     */
    core::u32 sharedCreators{0u}; ///< Rows naming nobody.
    core::u32 beforeNineteenHundred{0u}; ///< Rows dated before 1900 — the part of the shelf that is free.
};

/**
 * @brief Reads a HathiTrust holdings file into a baker.
 *
 * The format is a tab-separated line per volume with no header row, and the columns this
 * reads were confirmed against a real update file rather than taken from documentation:
 * 1 identifier, 2 access, 3 rights, 12 title, 17 year, 19 language, 26 author.
 *
 * @warning Rights are read from the holder's own code (`pd`, `pdus`, `world`…) and never inferred
 * from the year. A 1905 book may be in copyright and a 1960 government document may not be,
 * and a harvester that guessed would be writing a legal opinion into a data field.
 *
 * @param path      The uncompressed file. Gzip is the caller's business: a decompressor is a
 *                  dependency this repository does not have, and `gunzip` already exists.
 * @param holder    Identifier naming the repository; the caller names it in the vocabulary.
 * @param baker     Where to put it.
 * @param outReport Receives the tally.
 * @return false when the file could not be opened.
 */
[[nodiscard]] bool ingestHathiFile(const std::string &path, core::u32 holder, ICatalogueSink &sink,
                                   CatalogueIngestReport &outReport,
                                   std::vector<std::string> *outKeys = nullptr);

/**
 * @brief Reads Project Gutenberg's per-ebook RDF into a baker.
 *
 * One file per work, as the published catalogue archive contains them.
 *
 * @param paths     The RDF files.
 * @param holder    Identifier naming the repository.
 * @param baker     Where to put it.
 * @param outReport Receives the tally.
 * @return false when a file could not be opened.
 */
[[nodiscard]] bool ingestGutenbergRdf(const std::vector<std::string> &paths, core::u32 holder,
                                      ICatalogueSink &sink, CatalogueIngestReport &outReport);

/**
 * @brief Reads a year out of a field, or zero.
 *
 * Four digits, anywhere in the field, because holdings data writes `1976`, `c1976` and `[1976]`
 * interchangeably — and an OAI record writes `2023-01-15`. Anything else is NO year rather than
 * a guessed one; a catalogue that invents dates is worse than one that admits to gaps.
 *
 * Exposed because a second reader needed it, and two answers to "what year is this field"
 * would be two catalogues disagreeing about when the same book was printed.
 *
 * @param field The field.
 * @return The year, or 0.
 */
[[nodiscard]] core::i32 readYear(std::string_view field) noexcept;

/**
 * @brief Maps a language code onto the corpus tag.
 *
 * Two- and three-letter forms both, because which one arrives is the metadata format's choice
 * and not a fact about the language. Anything outside the enumeration is Unknown rather than
 * approximated.
 *
 * @param code The code.
 * @return The tag.
 */
[[nodiscard]] core::u32 languageOf(std::string_view code) noexcept;

/**
 * @struct CatalogueResolution
 * @brief How many distinct works a pile of holdings turned out to be.
 */
struct CatalogueResolution {
    core::u32 clusters{0u}; ///< Distinct works among the rows that had a key.
    core::u32 merged{0u};   ///< Rows that joined a work an earlier row had already named.
    core::u32 unkeyed{0u};  ///< Rows carrying no hard identifier. Left alone; see below.
    core::u32 largest{0u};  ///< Copies of the most-held work.
};

/**
 * @brief Groups holdings that are copies of one work.
 *
 * @warning **On a HARD key only, and that is this repository's rule rather than a shortcut.**
 * `EntityResolution` states it: a hard key merges, a score only proposes, because similarity is
 * not transitive and identity is. Two holdings of one work sit in different repositories under
 * different addresses, so there is no address to compare — what there IS, in real data, is a
 * shared bibliographic number. Measured on a HathiTrust update: column 8 carries an OCLC number
 * on **96 %** of rows, and 3624 of those numbers are shared by more than one volume.
 *
 * @warning **`cluster` is the one-based ROW INDEX of the first holding of that work, never a hash.**
 * A 32-bit hash over nineteen million keys collides about forty thousand times, and a collision
 * here does not lose a lookup — it declares two unrelated works to be the same, which is the
 * silent failure this whole module exists to prevent. An index is a position and cannot collide.
 *
 * @warning A row with no key keeps `cluster` at zero, which means UNRESOLVED and is deliberately not
 * the same as "resolved to be alone". Four per cent of a catalogue is a great many books, and
 * saying nothing about them is honest where guessing would not be.
 *
 * @warning **This does not stream.** It needs every key at once — some 780 MB for a full HathiTrust
 * file — so it is the in-memory path only. Deduplicating a streamed bake means a second pass
 * over the written image, which is its own piece of work and is not this one.
 *
 * @param entries The rows; their @c cluster is filled in place.
 * @param keys    One identity string per row, index-aligned. Empty means no key.
 * @param out     Receives the tally.
 * @return false when @p keys is not the same length as @p entries.
 */
[[nodiscard]] bool resolveCatalogue(std::vector<knowledge::CatalogueEntryV1> &entries,
                                    const std::vector<std::string> &keys, CatalogueResolution &out);

/**
 * @brief Splits a tab-separated line, without allocating a string per field.
 *
 * Exposed because it is the part with an off-by-one in it: a trailing empty field is a field,
 * and a reader that drops it shifts every column after the first empty one. Perfectly ordinary
 * holdings data has empty columns in the middle.
 *
 * @param line   The line, newline excluded.
 * @param out    Receives one view per column; cleared first.
 */
void splitTabs(std::string_view line, std::vector<std::string_view> &out);

} // namespace lpl::harvest

#endif // LPL_LPL_HARVEST_CATALOGUE_HPP
