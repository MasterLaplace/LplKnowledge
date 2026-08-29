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
 * is the same bytes. @warning That order is what a reader scans, so it is also what a caller gets
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
#    include <lpl/harvest/Relief.hpp>
#    include <lpl/knowledge/Types.hpp>

#    include <map>
#    include <string>
#    include <string_view>
#    include <vector>

namespace lpl::harvest {

/**
 * @class ICatalogueSink
 * @brief Where a catalogue reader puts what it reads.
 *
 * Two methods, because a catalogue bake needs exactly two growing things and both are
 * append-only. That is what lets the same reader fill either an in-memory @ref Baker or a
 * @ref CatalogueStream that spills to disk — and the second exists because the first was
 * measured to need 11.9 GB for HathiTrust's full holdings file.
 */
class ICatalogueSink {
public:
    virtual ~ICatalogueSink() = default;

    /**
     * @brief Appends one line of verbatim text.
     *
     * @param text The line.
     * @return Its zero-based index in the Texts section.
     */
    virtual core::u32 addText(std::string_view text) = 0;

    /**
     * @brief Appends one catalogue entry.
     *
     * @param entry The row.
     */
    virtual void addCatalogueEntry(const knowledge::CatalogueEntryV1 &entry) = 0;

    /**
     * @brief Announces that the texts and entry of ONE row are about to be written.
     *
     * @warning Exists for a sink that may split its output. A row's title, creator and address are
     * Texts LINE INDICES, and an index means a position in ONE image — so a sink that started a
     * new file between a row's second and third string would leave the row pointing at whatever
     * text happened to sit at that index in the new file. Silently, and about a book.
     *
     * Rolling can therefore only happen HERE, and the boundary has to be told rather than
     * guessed: a sink cannot know from `addText` alone whether it is the first string of a row
     * or the third. A default no-op, because a sink that never splits has nothing to do.
     */
    virtual void beginRow() {}
};

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
    core::u32 catalogue{0u};   ///< Catalogue entries written.
    core::u32 gazetteer{0u};   ///< Named places written.
    core::u32 placeLinks{0u};  ///< Attested connections written, both directions.
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
class Baker final : public ICatalogueSink {
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
    core::u32 addText(std::string_view text) override;

    /**
     * @brief Adds a catalogue entry: a work that exists somewhere, without its text.
     *
     * @param entry The record. Its string fields are Texts line indices, as returned by
     *              @ref addText — never hashed identifiers; see CatalogueEntryV1 for why.
     */
    void addCatalogueEntry(const knowledge::CatalogueEntryV1 &entry) override;

    /**
     * @brief The catalogue accumulated so far, so a resolver can fill in @c cluster.
     *
     * Handed out mutable because deciding that two rows are one work is a fact about the whole
     * batch: nothing that sees one row at a time can fill that field, which is exactly the
     * mistake `SourceV1::agreements` sat in for as long as it did.
     *
     * @return The entries, in insertion order.
     */
    [[nodiscard]] std::vector<knowledge::CatalogueEntryV1> &catalogue() noexcept { return _catalogue; }

    /**
     * @brief Records one named place.
     *
     * @param entry The place.
     */
    void addGazetteerEntry(const knowledge::GazetteerEntryV1 &entry) { _gazetteer.push_back(entry); }

    /**
     * @brief The places accumulated so far.
     *
     * @return The rows.
     */
    [[nodiscard]] const std::vector<knowledge::GazetteerEntryV1> &gazetteer() const noexcept
    {
        return _gazetteer;
    }

    /**
     * @brief Records that two places are connected, in both directions.
     *
     * @warning Both, from one call: the source data is directed and a road is not. See `PlaceLinkV1`.
     *
     * @param from One place.
     * @param to   The other.
     */
    void addPlaceLink(core::u32 from, core::u32 to)
    {
        _placeLinks.push_back(knowledge::PlaceLinkV1{from, to});
        _placeLinks.push_back(knowledge::PlaceLinkV1{to, from});
    }

    /**
     * @brief Records that two records MIGHT be one thing, without deciding that they are.
     *
     * @warning **One direction only, unlike a place link, and for the opposite reason.** A road is
     * walked both ways so its record is symmetric; a suspicion is a single question -- "are these
     * two the same?" -- and writing it twice would make one question look like two independent
     * ones, which is precisely the miscount that corroboration must never make. The pair is
     * always stored with the smaller identifier first so that the same question has one spelling.
     *
     * @param left     A mention identifier.
     * @param right    Another.
     * @param scoreRaw Resemblance, raw Q16.16.
     * @param evidence What suggested it; a `harvest::Evidence` value.
     */
    void addCandidate(core::u32 left, core::u32 right, core::u32 scoreRaw, core::u32 evidence)
    {
        const core::u32 low = left < right ? left : right;
        const core::u32 high = left < right ? right : left;
        _candidates.push_back(knowledge::CandidateV1{low, high, scoreRaw, evidence});
    }

    /**
     * @brief Records a credit this image must carry to be redistributable.
     *
     * @warning **Deduplicated, because 1344 Perseus works declare the same CC-BY-SA 4.0 line.** One
     * credit repeated per work would be a section larger than the facts it covers, and no more
     * informative -- what a reader needs is the SET of terms in the image, not a tally of them.
     *
     * @warning Verbatim. Every one of these lines is a condition of the permission to pass the image
     * on, and a summarised licence is a different licence.
     *
     * @param credit What the source requires.
     * @param covers What it applies to, or `knowledge::kNoIdentifier` for the whole image.
     */
    void addAttribution(std::string_view credit, core::u32 covers = knowledge::kNoIdentifier)
    {
        if (credit.empty())
            return;
        for (const Credited &held : _credits)
            if (held.text == credit && held.covers == covers)
                return;
        // The line is added to Texts HERE rather than at build time, because building is const and
        // because a credit is a fact about the image the moment a reader declares it.
        _credits.push_back(Credited{std::string{credit}, addText(credit) + 1u, covers});
    }

    /**
     * @brief Gives the image its measured ground.
     *
     * @warning **One survey, and the second call replaces the first rather than adding to it.** A world
     * has one ground; two relief sections would be two answers to how high a cell is, with nothing
     * downstream able to choose. The reader refuses an image carrying two, so producing one here
     * would be baking something that cannot be opened.
     *
     * @warning **It does NOT add the attribution.** The credit belongs to whoever knows which product
     * these samples came from -- this baker is handed a grid of numbers and cannot tell SRTM from a
     * fixture. Callers pass @ref reliefAttribution to @ref addAttribution; the round-trip test
     * asserts an image with relief and no credit is a defect rather than a shape.
     *
     * @param grid       Samples already in cell space. See @ref resampleToCells.
     * @param projection The one the samples were laid down under. Stored, never rebuilt.
     * @param blendCells Cells over which real ground gives way to invented ground at the edge.
     */
    void setRelief(const CellGrid &grid, const math::ReliefProjection &projection, core::u32 blendCells)
    {
        _relief = knowledge::ReliefV1{};
        _relief.width = grid.width;
        _relief.height = grid.height;
        _relief.originCellX = grid.originCellX;
        _relief.originCellZ = grid.originCellZ;
        _relief.latitudeRaw = projection.projection.originLatitudeRaw;
        _relief.longitudeRaw = projection.projection.originLongitudeRaw;
        _relief.referenceLatitude = projection.projection.referenceLatitude;
        _relief.metresPerCell = projection.projection.metresPerCell;
        _relief.unitsPerMetreRaw = projection.projection.unitsPerMetre.raw();
        _relief.seaLevelUnitsRaw = projection.projection.seaLevelUnits.raw();
        _relief.blendCells = blendCells;
        _relief.exposedEdges = 0xFu;
        _reliefSamples = grid.samples;
        _hasRelief = true;
    }

    /**
     * @brief Says which of this tile's edges face nothing.
     *
     * @param mask A mask of `math::kReliefEdge*` bits.
     */
    void setReliefExposedEdges(core::u32 mask) noexcept { _reliefExposedEdges = mask; }

    /// @return Whether a survey has been given to this image.
    [[nodiscard]] bool hasRelief() const noexcept { return _hasRelief; }

    /**
     * @brief Distinct credits accumulated so far.
     *
     * @return How many.
     */
    [[nodiscard]] core::u32 attributionCount() const noexcept
    {
        return static_cast<core::u32>(_credits.size());
    }

    /**
     * @brief Suspicions accumulated so far.
     *
     * @return How many pairs somebody should look at.
     */
    [[nodiscard]] core::u32 candidateCount() const noexcept
    {
        return static_cast<core::u32>(_candidates.size());
    }

    /**
     * @brief The connections accumulated so far.
     *
     * @return The pairs.
     */
    [[nodiscard]] const std::vector<knowledge::PlaceLinkV1> &placeLinks() const noexcept
    {
        return _placeLinks;
    }

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

    /**
     * @brief The two words that first collided on one identifier.
     *
     * @warning Exposed because @ref name returns a bare false, and a caller that gives up on it can
     * only report "something collided". `corpus::nameIdentifier` is a 32-bit hash over a
     * corpus that may hold a million names, so a collision is a NORMAL event at scale rather
     * than a corrupt input — and the reader that hits one has to be able to say which two.
     *
     * @return "a / b", or empty when nothing has collided.
     */
    [[nodiscard]] const std::string &firstCollision() const noexcept { return _firstCollision; }

private:
    struct Named {
        core::u32 id;
        std::string text;
    };

    std::vector<Named> _names;
    /**
     * Where each identifier sits in @ref _names.
     *
     * @warning Not a cache: without it @ref name is a linear scan, so interning a corpus is
     * quadratic in its size. Measured on Perseus, whose 2042 works carry some 700 000
     * passages between them — the scan alone is where the time goes.
     */
    std::map<core::u32, std::size_t> _byIdentifier;
    std::vector<knowledge::DocumentV1> _documents;
    std::vector<knowledge::LocusV1> _loci;
    std::vector<knowledge::SourceV1> _sources;
    std::vector<knowledge::FactV1> _facts;
    std::vector<std::string> _texts;
    std::vector<knowledge::CatalogueEntryV1> _catalogue;
    std::vector<knowledge::GazetteerEntryV1> _gazetteer;
    std::vector<knowledge::PlaceLinkV1> _placeLinks;
    struct Credited {
        std::string text; ///< Kept for deduplication; the image carries the line, not this.
        core::u32 line;   ///< One-based Texts line.
        core::u32 covers;
    };
    std::vector<Credited> _credits;

    knowledge::ReliefV1 _relief{};        ///< At most one: a world has one ground.
    std::vector<core::i16> _reliefSamples;
    bool _hasRelief{false};
    core::u32 _reliefExposedEdges{0xFu};
    std::vector<knowledge::CandidateV1> _candidates;
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
