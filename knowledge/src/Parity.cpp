/**
 * @file Parity.cpp
 * @brief Implementation of gate P18 `corpus`: a corpus that survives being written down.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/knowledge/Parity.hpp>

#if defined(LPL_HAS_FOUNDATION)

#    include <lpl/history/Parity.hpp>
#    include <lpl/knowledge/FactStore.hpp>
#    include <lpl/knowledge/History.hpp>
#    include <lpl/knowledge/Provenance.hpp>

namespace lpl::knowledge {

namespace {

/**
 * @brief Folds every claim an image holds.
 *
 * Over the WIRE records rather than the decoded ones, so the signature says something
 * about the format and not about the decoder: a decoder that dropped a field would leave
 * this unchanged and move @ref KnowledgeFoldResult::roundTrip instead, which is how the two
 * failures stay told apart.
 *
 * @param pack The image.
 * @return The signature.
 */
[[nodiscard]] core::u32 foldFacts(const KnowledgePack &pack) noexcept
{
    core::u32 hash = kFnv1aOffsetBasis;
    for (core::u32 i = 0u; i < pack.factCount(); ++i)
    {
        FactV1 fact{};
        if (!pack.factAt(i, fact))
            continue;
        foldWord(hash, fact.subject);
        foldWord(hash, fact.predicate);
        foldWord(hash, fact.object);
        foldWord(hash, static_cast<core::u32>(fact.fromYear));
        foldWord(hash, static_cast<core::u32>(fact.toYear));
        foldWord(hash, fact.source);
        foldWord(hash, fact.confidenceRaw);
        foldWord(hash, fact.locus);
    }
    foldWord(hash, pack.factCount());
    return hash;
}

/**
 * @brief Folds the names an image carries, identifier and word together.
 *
 * @param pack The image.
 * @return The signature.
 */
[[nodiscard]] core::u32 foldVocabulary(const KnowledgePack &pack) noexcept
{
    core::u32 hash = kFnv1aOffsetBasis;

    // Walked through the sources and the facts rather than over the section, because the
    // section is not exposed record by record — and because what matters is that the
    // identifiers a claim USES are the ones the image names.
    for (core::u32 i = 0u; i < pack.factCount(); ++i)
    {
        FactV1 fact{};
        if (!pack.factAt(i, fact))
            continue;
        const core::u32 ids[] = {fact.subject, fact.predicate, fact.object, fact.source};
        for (const core::u32 id : ids)
        {
            foldWord(hash, id);
            const char *text = pack.textFor(id);
            if (text == nullptr)
            {
                foldWord(hash, 0u);
                continue;
            }
            core::u32 length = 0u;
            while (text[length] != '\0')
                ++length;
            foldBytes(hash, reinterpret_cast<const core::u8 *>(text), length);
            foldWord(hash, length);
        }
    }
    foldWord(hash, pack.vocabularyCount());
    return hash;
}

} // namespace

core::u32 parityQuerySubject() noexcept { return history::kSubjectKing; }

void foldKnowledgeState(const core::u8 *image, core::u32 size, KnowledgeFoldResult &out)
{
    out = KnowledgeFoldResult{};
    out.imageBytes = size;
    out.imageSignature = foldImage(image, size);

    KnowledgePack pack;
    const OpenStatus status = pack.open(image, size);
    out.openStatus = static_cast<core::u32>(status);
    if (status != OpenStatus::Ok)
        return;

    out.sections = pack.sectionCount();
    out.skipped = pack.skippedSections();
    out.facts = pack.factCount();
    out.sources = pack.sourceCount();
    out.documents = pack.documentCount();
    out.loci = pack.locusCount();
    out.vocabulary = pack.vocabularyCount();

    out.factSignature = foldFacts(pack);
    out.vocabularySignature = foldVocabulary(pack);

    ProvenanceAudit audit{};
    out.provenanceOk = auditProvenance(pack, audit) ? 1u : 0u;
    out.auditSignature = foldAudit(audit);

    // ── The canonical query ───────────────────────────────────────────────────
    //
    // Everything the image holds about the king in the year the sources disagree about.
    // Deliberately the contested case: a query over the uncontested capital would pass on
    // an image that had lost one of two contradictory claims.
    Query query;
    query.about(parityQuerySubject()).during(1204).take(8u);

    const FactStore store{pack};
    // In BSS rather than on the stack: a page is two kibibytes and this function runs in
    // ring 0, where that is a real fraction of a kernel stack. Static is safe here because
    // the gate is single-threaded by construction — it runs once, from the smoke battery.
    static Page page;
    store.run(query, page);
    out.pageSignature = FactStore::foldPage(page);
    out.queryMatched = page.matched;
    out.queryReturned = page.count;
    out.queryTruncated = page.truncated;

    // ── The citation ──────────────────────────────────────────────────────────
    //
    // Rendered and folded, because a locus that is stored but never resolved is a field
    // nobody has checked. The claim chosen is the chronicler's — the one the canonical case
    // is an argument about.
    for (core::u32 i = 0u; i < page.count; ++i)
    {
        if (page.rows[i].source != history::kSourceChronicler)
            continue;
        Citation citation{};
        if (!cite(pack, page.rows[i], citation))
            break;
        char line[192];
        const core::u32 length = renderCitation(citation, line, 192u);
        core::u32 hash = kFnv1aOffsetBasis;
        foldBytes(hash, reinterpret_cast<const core::u8 *>(line), length);
        foldWord(hash, length);
        out.citationSignature = hash;
        break;
    }

    // ── The round trip ────────────────────────────────────────────────────────
    history::Corpus decoded;
    DecodeReport report{};
    const bool clean = toHistoryCorpus(pack, decoded, report);
    out.decodeRejected = report.badKind + report.badWindow + report.badConfidence;
    if (!clean)
        return;

    history::Corpus authored;
    history::parityCorpus(authored);
    out.roundTrip = corporaMatch(decoded, authored) ? 1u : 0u;

    // The three signatures that must equal gate P13. Run on the DECODED corpus, which is
    // the whole point: P13 runs the same pipeline on the corpus held in memory, so an
    // equality here says the image gave back what was put in — and an inequality says it
    // did not, without anyone having to guess which field was lost.
    history::HistoryFoldResult replay{};
    history::foldHistoryCorpus(decoded, replay);
    out.timelineSignature = replay.timelineSignature;
    out.chronicleSignature = replay.chronicleSignature;
    out.minoritySignature = replay.minoritySignature;
    out.consensusObject = replay.consensusObject;
}

} // namespace lpl::knowledge

#endif // LPL_HAS_FOUNDATION
