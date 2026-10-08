/**
 * @file Baker.cpp
 * @brief Implementation of emitting the .lplknow image the freestanding side reads.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/Baker.hpp>

#include <lpl/corpus/Language.hpp>
#include <lpl/corpus/Urn.hpp>

#include <algorithm>

#include <lpl/history/Parity.hpp>
#include <lpl/knowledge/History.hpp>

namespace lpl::harvest {

namespace {

/**
 * @brief Appends a little-endian word to a byte buffer.
 *
 * Assembled byte by byte rather than memcpy'd from a @c u32, so the image is
 * little-endian because it was WRITTEN little-endian and not because the host happened to
 * be. A big-endian writer would otherwise produce an image every reader silently
 * misreads.
 *
 * @param out   Destination.
 * @param value What to append.
 */
void pushWord(std::vector<core::u8> &out, core::u32 value)
{
    out.push_back(static_cast<core::u8>(value & 0xFFu));
    out.push_back(static_cast<core::u8>((value >> 8) & 0xFFu));
    out.push_back(static_cast<core::u8>((value >> 16) & 0xFFu));
    out.push_back(static_cast<core::u8>((value >> 24) & 0xFFu));
}

/**
 * @brief Does one claim sort before another?
 *
 * Subject, then window start, then predicate, object and source. Every field is compared
 * so the order is TOTAL: a partial order would let two claims that compare equal come out
 * in whichever sequence the sort happened to leave them, and the image would stop being a
 * function of the corpus.
 *
 * @param a First claim.
 * @param b Second claim.
 * @return true when @p a comes first.
 */
[[nodiscard]] bool factPrecedes(const knowledge::FactV1 &a, const knowledge::FactV1 &b) noexcept
{
    if (a.subject != b.subject)
        return a.subject < b.subject;
    if (a.fromDay != b.fromDay)
        return a.fromDay < b.fromDay;
    if (a.predicate != b.predicate)
        return a.predicate < b.predicate;
    if (a.object != b.object)
        return a.object < b.object;
    if (a.source != b.source)
        return a.source < b.source;
    if (a.toDay != b.toDay)
        return a.toDay < b.toDay;
    return a.confidenceRaw < b.confidenceRaw;
}

} // namespace

bool Baker::name(core::u32 id, std::string_view text)
{
    const auto at = _byIdentifier.find(id);
    if (at != _byIdentifier.end())
    {
        Named &entry = _names[at->second];
        if (entry.text == text)
            return true; // named twice with the same word: idempotent, not a problem
        ++_collisions;
        if (_firstCollision.empty())
            _firstCollision = entry.text + " / " + std::string{text};
        return false;
    }
    _byIdentifier.emplace(id, _names.size());
    _names.push_back(Named{id, std::string{text}});
    return true;
}

core::u32 Baker::addDocument(const knowledge::DocumentV1 &document)
{
    _documents.push_back(document);
    return static_cast<core::u32>(_documents.size()); // one-based
}

core::u32 Baker::addLocus(core::u32 documentIndex, const corpus::Locus &locus)
{
    knowledge::LocusV1 wire{};
    // Stored zero-based, because the reader indexes an array with it; the ONE-based value
    // is what a fact carries, so that zero can mean "no locus". The two conventions meet
    // here and nowhere else.
    wire.document = documentIndex == 0u ? 0u : documentIndex - 1u;
    wire.part = locus.part;
    wire.section = locus.section;
    wire.line = locus.line;
    _loci.push_back(wire);
    return static_cast<core::u32>(_loci.size());
}

void Baker::addSource(const knowledge::SourceV1 &source) { _sources.push_back(source); }

void Baker::addCatalogueEntry(const knowledge::CatalogueEntryV1 &entry)
{
    _catalogue.push_back(entry);
}

void Baker::addFact(const knowledge::FactV1 &fact) { _facts.push_back(fact); }

core::u32 Baker::addText(std::string_view text)
{
    _texts.emplace_back(text);
    return static_cast<core::u32>(_texts.size() - 1u);
}

bool Baker::build(std::vector<core::u8> &out, BakeReport &report) const
{
    out.clear();
    report = BakeReport{};
    report.collisions = _collisions;
    report.firstCollision = _firstCollision;

    if (_collisions != 0u)
        return false;

    // Every claim's source has to be described, and this is the only place that can check
    // it: Provenance.hpp calls a claim whose source is unknown unweighable, so writing one
    // would produce an image that opens cleanly and answers wrongly.
    for (const knowledge::FactV1 &fact : _facts)
    {
        const bool described =
            std::any_of(_sources.begin(), _sources.end(),
                        [&fact](const knowledge::SourceV1 &source) { return source.id == fact.source; });
        if (!described)
            ++report.unsourced;
    }
    if (report.unsourced != 0u)
        return false;

    std::vector<knowledge::FactV1> facts = _facts;
    std::sort(facts.begin(), facts.end(), factPrecedes);

    std::vector<Named> names = _names;
    std::sort(names.begin(), names.end(), [](const Named &a, const Named &b) { return a.id < b.id; });

    // ── Sections, built as standalone payloads first ──────────────────────────
    std::vector<core::u8> vocabulary;
    if (!names.empty())
    {
        std::vector<core::u8> text;
        std::vector<core::u32> offsets;
        offsets.reserve(names.size());
        for (const Named &entry : names)
        {
            offsets.push_back(static_cast<core::u32>(text.size()));
            for (const char c : entry.text)
                text.push_back(static_cast<core::u8>(c));
            text.push_back(0u); // NUL-terminated: the reader returns a C string view
        }

        pushWord(vocabulary, static_cast<core::u32>(names.size()));
        pushWord(vocabulary, static_cast<core::u32>(text.size()));
        for (core::usize i = 0u; i < names.size(); ++i)
        {
            pushWord(vocabulary, names[i].id);
            pushWord(vocabulary, offsets[i]);
        }
        vocabulary.insert(vocabulary.end(), text.begin(), text.end());
    }

    std::vector<core::u8> sources;
    for (const knowledge::SourceV1 &source : _sources)
    {
        pushWord(sources, source.id);
        pushWord(sources, source.kind);
        pushWord(sources, source.yearsAfterEvent);
        pushWord(sources, source.agreements);
        pushWord(sources, source.name);
        pushWord(sources, source.document);
        // The composition window, in the same order the reader casts it back. Two more words
        // rather than a new section: this is not optional information about a source, it is part
        // of what a source IS, and a section exists for what is consumed separately.
        pushWord(sources, static_cast<core::u32>(source.composedFrom));
        pushWord(sources, static_cast<core::u32>(source.composedTo));
        pushWord(sources, source.flags);
    }

    std::vector<core::u8> factBytes;
    for (const knowledge::FactV1 &fact : facts)
    {
        pushWord(factBytes, fact.subject);
        pushWord(factBytes, fact.predicate);
        pushWord(factBytes, fact.object);
        pushWord(factBytes, static_cast<core::u32>(fact.fromDay));
        pushWord(factBytes, static_cast<core::u32>(fact.toDay));
        pushWord(factBytes, fact.source);
        pushWord(factBytes, fact.confidenceRaw);
        pushWord(factBytes, fact.locus);
    }

    std::vector<core::u8> documents;
    for (const knowledge::DocumentV1 &document : _documents)
    {
        pushWord(documents, document.urn);
        pushWord(documents, document.title);
        pushWord(documents, document.language);
        pushWord(documents, document.flags);
    }

    // The Texts section: a prologue, then lineCount + 1 offsets, then the bytes. The extra
    // offset is what makes the LAST line's length a subtraction like every other — storing
    // lengths instead would have made the final line the one case needing a different rule,
    // which is where an off-by-one lives.
    std::vector<core::u8> texts;
    if (!_texts.empty())
    {
        std::vector<core::u8> body;
        std::vector<core::u32> offsets;
        offsets.reserve(_texts.size() + 1u);
        for (const std::string &line : _texts)
        {
            offsets.push_back(static_cast<core::u32>(body.size()));
            for (const char c : line)
                body.push_back(static_cast<core::u8>(c));
        }
        offsets.push_back(static_cast<core::u32>(body.size()));

        pushWord(texts, static_cast<core::u32>(_texts.size()));
        pushWord(texts, static_cast<core::u32>(body.size()));
        for (const core::u32 offset : offsets)
            pushWord(texts, offset);
        texts.insert(texts.end(), body.begin(), body.end());
    }

    std::vector<core::u8> catalogue;
    if (!_catalogue.empty())
    {
        pushWord(catalogue, static_cast<core::u32>(_catalogue.size()));
        pushWord(catalogue, 0u); // reserved
        for (const knowledge::CatalogueEntryV1 &entry : _catalogue)
        {
            pushWord(catalogue, entry.title);
            pushWord(catalogue, entry.creator);
            pushWord(catalogue, entry.address);
            pushWord(catalogue, entry.holder);
            pushWord(catalogue, static_cast<core::u32>(entry.year));
            pushWord(catalogue, entry.language);
            pushWord(catalogue, entry.flags);
            pushWord(catalogue, entry.cluster);
            pushWord(catalogue, static_cast<core::u32>(entry.yearTo));
        }
    }

    std::vector<core::u8> gazetteer;
    if (!_gazetteer.empty())
    {
        pushWord(gazetteer, static_cast<core::u32>(_gazetteer.size()));
        pushWord(gazetteer, 0u); // reserved
        for (const knowledge::GazetteerEntryV1 &entry : _gazetteer)
        {
            pushWord(gazetteer, entry.place);
            pushWord(gazetteer, entry.title);
            pushWord(gazetteer, static_cast<core::u32>(entry.latRaw));
            pushWord(gazetteer, static_cast<core::u32>(entry.lonRaw));
            pushWord(gazetteer, static_cast<core::u32>(entry.minYear));
            pushWord(gazetteer, static_cast<core::u32>(entry.maxYear));
            pushWord(gazetteer, entry.flags);
            pushWord(gazetteer, entry.kinds);
        }
    }

    std::vector<core::u8> placeLinks;
    for (const knowledge::PlaceLinkV1 &link : _placeLinks)
    {
        pushWord(placeLinks, link.from);
        pushWord(placeLinks, link.to);
    }

    // Emitted before the Texts section is sealed, because each credit becomes a line in it.
    std::vector<core::u8> attributions;
    for (const Credited &credit : _credits)
    {
        pushWord(attributions, credit.line);
        pushWord(attributions, credit.covers);
    }

    std::vector<core::u8> candidates;
    for (const knowledge::CandidateV1 &candidate : _candidates)
    {
        pushWord(candidates, candidate.left);
        pushWord(candidates, candidate.right);
        pushWord(candidates, candidate.scoreRaw);
        pushWord(candidates, candidate.evidence);
    }

    std::vector<core::u8> loci;
    for (const knowledge::LocusV1 &locus : _loci)
    {
        pushWord(loci, locus.document);
        pushWord(loci, locus.part);
        pushWord(loci, locus.section);
        pushWord(loci, locus.line);
    }

    // The relief section: a fixed header then the samples. Written by hand rather than by
    // memcpy of the struct, for the reason every other section is: a struct copy would bake this
    // machine's padding and alignment into the wire, and the reader is a different compiler.
    std::vector<core::u8> relief;
    if (_hasRelief && !_reliefSamples.empty())
    {
        pushWord(relief, _relief.width);
        pushWord(relief, _relief.height);
        pushWord(relief, static_cast<core::u32>(_relief.originCellX));
        pushWord(relief, static_cast<core::u32>(_relief.originCellZ));
        pushWord(relief, static_cast<core::u32>(_relief.latitudeRaw));
        pushWord(relief, static_cast<core::u32>(_relief.longitudeRaw));
        pushWord(relief, static_cast<core::u32>(_relief.referenceLatitude));
        pushWord(relief, _relief.metresPerCell);
        pushWord(relief, static_cast<core::u32>(_relief.unitsPerMetreRaw));
        pushWord(relief, static_cast<core::u32>(_relief.seaLevelUnitsRaw));
        pushWord(relief, _relief.blendCells);
        pushWord(relief, _reliefExposedEdges);
        for (const core::i16 sample : _reliefSamples)
        {
            const auto word = static_cast<core::u16>(sample);
            relief.push_back(static_cast<core::u8>(word & 0xFFu));
            relief.push_back(static_cast<core::u8>((word >> 8) & 0xFFu));
        }
    }

    struct Pending {
        knowledge::SectionType type;
        const std::vector<core::u8> *payload;
    };

    // Sections are emitted in a fixed order and empty ones are omitted. Omitted rather
    // than written empty, because an image that names a section it has nothing for makes
    // "no vocabulary was baked" and "the vocabulary is empty" the same statement.
    const Pending pending[] = {
        {knowledge::SectionType::Vocabulary, &vocabulary},
        {knowledge::SectionType::Sources, &sources},
        {knowledge::SectionType::Facts, &factBytes},
        {knowledge::SectionType::Documents, &documents},
        {knowledge::SectionType::Loci, &loci},
        {knowledge::SectionType::Texts, &texts},
        {knowledge::SectionType::Catalogue, &catalogue},
        {knowledge::SectionType::Gazetteer, &gazetteer},
        {knowledge::SectionType::PlaceLink, &placeLinks},
        {knowledge::SectionType::Candidate, &candidates},
        {knowledge::SectionType::Attribution, &attributions},
        {knowledge::SectionType::Relief, &relief},
    };

    core::u32 sectionCount = 0u;
    for (const Pending &entry : pending)
        if (!entry.payload->empty())
            ++sectionCount;

    // ── Layout ────────────────────────────────────────────────────────────────
    const core::u32 headerBytes = static_cast<core::u32>(sizeof(knowledge::Header));
    const core::u32 tableBytes = sectionCount * static_cast<core::u32>(sizeof(knowledge::SectionEntry));

    std::vector<core::u32> offsets;
    core::u32 cursor = headerBytes + tableBytes;
    for (const Pending &entry : pending)
    {
        if (entry.payload->empty())
            continue;
        cursor = (cursor + 3u) & ~3u; // every section offset is a multiple of four
        offsets.push_back(cursor);
        cursor += static_cast<core::u32>(entry.payload->size());
    }
    const core::u32 totalSize = cursor;

    out.reserve(totalSize);

    // Header. The content hash is written last, once the bytes it covers exist — a hash
    // computed over a buffer that is still being appended to is a hash of something else.
    const char magic[] = {'L', 'P', 'L', 'K', 'N', 'O', 'W', '\0'};
    for (const char c : magic)
        out.push_back(static_cast<core::u8>(c));
    pushWord(out, knowledge::kFormatVersion);
    pushWord(out, totalSize);
    pushWord(out, sectionCount);
    pushWord(out, 0u); // content hash, patched below
    pushWord(out, 0u); // eccOffset — no parity section yet
    pushWord(out, 0u); // eccSize

    core::usize which = 0u;
    for (const Pending &entry : pending)
    {
        if (entry.payload->empty())
            continue;
        pushWord(out, static_cast<core::u32>(entry.type));
        pushWord(out, offsets[which]);
        pushWord(out, static_cast<core::u32>(entry.payload->size()));
        pushWord(out, 0u); // reserved
        ++which;
    }

    which = 0u;
    for (const Pending &entry : pending)
    {
        if (entry.payload->empty())
            continue;
        while (out.size() < offsets[which])
            out.push_back(0u);
        out.insert(out.end(), entry.payload->begin(), entry.payload->end());
        ++which;
    }

    if (static_cast<core::u32>(out.size()) != totalSize)
        return false; // the layout pass and the write pass disagreed; never ship that

    core::u32 hash = knowledge::kFnv1aOffsetBasis;
    knowledge::foldBytes(hash, out.data() + headerBytes, totalSize - headerBytes);
    out[20] = static_cast<core::u8>(hash & 0xFFu);
    out[21] = static_cast<core::u8>((hash >> 8) & 0xFFu);
    out[22] = static_cast<core::u8>((hash >> 16) & 0xFFu);
    out[23] = static_cast<core::u8>((hash >> 24) & 0xFFu);

    report.facts = static_cast<core::u32>(facts.size());
    report.sources = static_cast<core::u32>(_sources.size());
    report.documents = static_cast<core::u32>(_documents.size());
    report.loci = static_cast<core::u32>(_loci.size());
    report.vocabulary = static_cast<core::u32>(names.size());
    report.textLines = static_cast<core::u32>(_texts.size());
    report.textBytes = texts.empty() ? 0u : static_cast<core::u32>(texts.size());
    report.catalogue = static_cast<core::u32>(_catalogue.size());
    report.gazetteer = static_cast<core::u32>(_gazetteer.size());
    report.placeLinks = static_cast<core::u32>(_placeLinks.size());
    report.sections = sectionCount;
    report.bytes = totalSize;
    return true;
}

bool bakeParityCorpus(std::vector<core::u8> &out, BakeReport &report)
{
    history::Corpus corpus;
    history::parityCorpus(corpus);

    Baker baker;

    // A document for the chronicle, so the image exercises the locus path rather than only
    // declaring it. A citation that no baked image ever carries is a field nobody has
    // checked, which is the orphan this project refuses elsewhere.
    knowledge::DocumentV1 chronicleDocument{};
    chronicleDocument.urn = corpus::nameIdentifier("urn:cts:parityLit:chronicle", 27u);
    chronicleDocument.title = corpus::nameIdentifier("chronicle-of-the-realm", 22u);
    chronicleDocument.language = static_cast<core::u32>(corpus::LanguageTag::EcclesiasticalLatin);
    chronicleDocument.flags = knowledge::kDocumentFlagPublicDomain;
    const core::u32 documentIndex = baker.addDocument(chronicleDocument);

    bool named = true;
    named = baker.name(chronicleDocument.urn, "urn:cts:parityLit:chronicle") && named;
    named = baker.name(chronicleDocument.title, "chronicle-of-the-realm") && named;

    // The words for the identifiers `history::Parity` declares. Written out rather than
    // derived, because these identifiers are small integers chosen by hand on that side —
    // hashing a name here would produce different ones and break the verbatim rule.
    struct NamedIdentifier {
        core::u32 id;
        const char *text;
    };
    const NamedIdentifier vocabulary[] = {
        {history::kSubjectKing, "king"},
        {history::kSubjectCapital, "capital"},
        {history::kSubjectOutpost, "outpost"},
        {history::kPredicateDiedOf, "died-of"},
        {history::kPredicateExists, "exists"},
        {history::kObjectBattle, "battle"},
        {history::kObjectDysentery, "dysentery"},
        {history::kObjectTrue, "true"},
        {history::kSourceChronicler, "chronicler"},
        {history::kSourceOsteology, "osteology"},
        {history::kSourceCharter, "charter"},
        {history::kSourceSurvey, "survey"},
    };
    for (const NamedIdentifier &entry : vocabulary)
        named = baker.name(entry.id, entry.text) && named;

    for (core::usize i = 0u; i < corpus.sources.size(); ++i)
    {
        const history::SourceProfile &profile = corpus.sources[i];
        knowledge::SourceV1 wire{};
        // Only the chronicler is tied to a document: the osteologist read bones and the
        // charter is itself the record, so a work reference for either would be a citation
        // invented to make a field non-empty.
        const core::u32 document = profile.id == history::kSourceChronicler ? documentIndex : knowledge::kNoIdentifier;
        knowledge::toWireSource(profile, profile.id, document, wire);
        baker.addSource(wire);
    }

    // One locus, for the claim the chronicler makes about how the king died — the claim the
    // whole canonical case is about, so the one whose provenance is worth being able to
    // check.
    const core::u32 locusIndex = baker.addLocus(documentIndex, corpus::Locus{3u, 12u, 412u});

    for (core::usize i = 0u; i < corpus.facts.size(); ++i)
    {
        const history::Fact &fact = corpus.facts[i];
        const bool cited = fact.source == history::kSourceChronicler && fact.predicate == history::kPredicateDiedOf;
        knowledge::FactV1 wire{};
        knowledge::toWireFact(fact, cited ? locusIndex : knowledge::kNoIdentifier, wire);
        baker.addFact(wire);
    }

    if (!named)
    {
        // A collision among a dozen hand-picked names would mean the derivation itself is
        // broken, so it is reported through the same refusal rather than tolerated here.
        std::vector<core::u8> discarded;
        (void) baker.build(discarded, report);
        return false;
    }

    return baker.build(out, report);
}

} // namespace lpl::harvest
