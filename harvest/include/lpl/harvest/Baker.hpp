/**
 * @file Baker.hpp
 * @brief Emitting the .lplknow image the freestanding side reads.
 *
 * The writer half of the reader/writer line. Indexes are computed here, once, so
 * that the ring-0 reader never has to build one.
 *
 * Two properties this class exists to guarantee, and both are refusals rather than
 * best-effort:
 *
 * **A collision is fatal.** `corpus::workIdentifier` is a 32-bit hash, so two canonical
 * names can land on one identifier — and the consequence is not a lost lookup, it is two
 * people silently becoming one person, with every claim about either appearing to be a
 * claim about both, and source corroboration counting them as agreement. Only a writer
 * that sees the whole corpus can detect it, so only a writer can refuse it, and this one
 * does.
 *
 * **The order is canonical.** Facts are sorted by subject, then window start, then
 * predicate, object and source — a total order, so the same corpus baked on two machines
 * is the same bytes. ⚠ That order is what a reader scans, so it is also what a caller gets
 * back: a corpus whose authored order differs from the canonical one round-trips as the
 * same SET of claims in a different sequence. It is exact for the canonical corpus because
 * that corpus is already authored in canonical order, which `test-knowledge-parity`
 * asserts rather than assumes.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_BAKER_HPP
#    define LPL_LPL_HARVEST_BAKER_HPP

#    include <lpl/Foundation.hpp>
#    include <lpl/corpus/Locus.hpp>
#    include <lpl/knowledge/Types.hpp>

#    include <string>
#    include <string_view>
#    include <vector>

namespace lpl::harvest {

/**
 * @struct BakeReport
 * @brief What was written, and what stopped it.
 */
struct BakeReport {
    core::u32 facts{0u};       ///< Claims written.
    core::u32 sources{0u};     ///< Source profiles written.
    core::u32 documents{0u};   ///< Documents written.
    core::u32 loci{0u};        ///< Loci written.
    core::u32 vocabulary{0u};  ///< Identifiers named.
    core::u32 textLines{0u};   ///< Verbatim lines carried.
    core::u32 textBytes{0u};   ///< Bytes of those lines.
    core::u32 sections{0u};    ///< Sections in the table.
    core::u32 bytes{0u};       ///< Size of the image.
    core::u32 collisions{0u};  ///< Identifiers claimed by two different names. FATAL.
    core::u32 unsourced{0u};   ///< Claims whose source is not described. FATAL.
    std::string firstCollision; ///< The two names that collided, for the message.
};

/**
 * @class Baker
 * @brief Accumulates a corpus and lays it out as an image.
 */
class Baker {
public:
    /**
     * @brief Records that an identifier reads as a name.
     *
     * @param id   The identifier.
     * @param text What it reads as.
     * @return false when @p id is already recorded under a DIFFERENT name — a collision,
     *         which is fatal and is counted in the report.
     */
    [[nodiscard]] bool name(core::u32 id, std::string_view text);

    /**
     * @brief Adds a document.
     *
     * @param document The record.
     * @return Its ONE-BASED index, so that zero keeps meaning "none".
     */
    core::u32 addDocument(const knowledge::DocumentV1 &document);

    /**
     * @brief Adds a locus.
     *
     * @param documentIndex One-based document index, as returned by @ref addDocument.
     * @param locus         Where in it.
     * @return Its one-based index.
     */
    core::u32 addLocus(core::u32 documentIndex, const corpus::Locus &locus);

    /**
     * @brief Adds a source profile.
     *
     * @param source The record.
     */
    void addSource(const knowledge::SourceV1 &source);

    /**
     * @brief Adds a claim.
     *
     * @param fact The record.
     */
    void addFact(const knowledge::FactV1 &fact);

    /**
     * @brief Adds one line of verbatim text.
     *
     * What makes an index a rendered VIEW rather than a table somebody maintains: the words
     * of a definition travel with its identifier, so the index can be regenerated instead of
     * edited. Kept as whole lines rather than as a blob with offsets computed later, because
     * a line is what a locus addresses and the two must not be able to disagree.
     *
     * @param text The line, newline excluded.
     * @return Its ZERO-based index in the text section.
     */
    core::u32 addText(std::string_view text);

    /**
     * @brief Lays everything out as a byte image.
     *
     * @param out    Receives the image; cleared first.
     * @param report Receives what was written or what stopped it.
     * @return false when the corpus cannot be baked — a collision, or a claim whose source
     *         nothing describes. Both are conditions under which the image would be
     *         readable and WRONG, which is worse than a refusal.
     */
    [[nodiscard]] bool build(std::vector<core::u8> &out, BakeReport &report) const;

private:
    struct Named {
        core::u32 id;
        std::string text;
    };

    std::vector<Named> _names;
    std::vector<knowledge::DocumentV1> _documents;
    std::vector<knowledge::LocusV1> _loci;
    std::vector<knowledge::SourceV1> _sources;
    std::vector<knowledge::FactV1> _facts;
    std::vector<std::string> _texts;
    mutable core::u32 _collisions{0u};
    mutable std::string _firstCollision;
};

#    if defined(LPL_HAS_FOUNDATION)

/**
 * @brief Bakes the canonical corpus of gate P13 into an image.
 *
 * The corpus is NOT re-declared here. It is read from `history::parityCorpus`, which is
 * the one place in the project that says a chronicler wrote forty years later with a
 * confidence of 0.45 — and if the corpus were spelled out on both sides of a round-trip
 * test, the test would be comparing two transcriptions rather than testing a format.
 *
 * The names come from `history::Parity`'s identifier enumeration, so the image carries a
 * vocabulary a human can read and the round trip still preserves the identifiers verbatim.
 *
 * @param out    Receives the image.
 * @param report Receives what was written.
 * @return false when the bake was refused.
 */
[[nodiscard]] bool bakeParityCorpus(std::vector<core::u8> &out, BakeReport &report);

#    endif // LPL_HAS_FOUNDATION

} // namespace lpl::harvest

#endif // LPL_LPL_HARVEST_BAKER_HPP
