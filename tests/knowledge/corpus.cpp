#include <lpl/history/Parity.hpp>
#include <lpl/knowledge/FactStore.hpp>
#include <lpl/knowledge/History.hpp>
#include <lpl/knowledge/KnowledgePack.hpp>
#include <lpl/knowledge/Parity.hpp>
#include <lpl/knowledge/ParityKnowBlob.hpp>
#include <lpl/knowledge/Provenance.hpp>
#include <lpl/knowledge/Types.hpp>
#include <lpl/testing/Test.hpp>

#include <cstddef>

LPL_TEST_SUITE(corpus);

namespace {

[[nodiscard]] bool sameText(const char *lhs, const char *rhs)
{
    while (*lhs != '\0' && *lhs == *rhs)
    {
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

[[nodiscard]] bool contains(const char *text, const char *needle)
{
    for (; *text != '\0'; ++text)
    {
        lpl::core::u32 matched = 0u;

        while (needle[matched] != '\0' && text[matched] == needle[matched])
            ++matched;
        if (needle[matched] == '\0')
            return true;
    }
    return false;
}

[[nodiscard]] lpl::knowledge::OpenStatus openEmbeddedImage(lpl::knowledge::KnowledgePack &pack)
{
    return pack.open(lpl::knowledge::kParityKnowledgeImage, lpl::knowledge::kParityKnowledgeImageSize);
}

} // namespace

/**
 * @brief The embedded image opens with no section skipped, and its vocabulary names an identifier
 *        and leaves an unnamed one absent rather than failing.
 */
LPL_TEST(the_embedded_image_opens_and_names_its_identifiers)
{
    lpl::knowledge::KnowledgePack pack;

    test.check(openEmbeddedImage(pack) == lpl::knowledge::OpenStatus::Ok, "the embedded image opens");
    test.check(pack.skippedSections() == 0u, "nothing was skipped");
    test.check(pack.textFor(lpl::history::kSubjectKing) != nullptr &&
                   sameText(pack.textFor(lpl::history::kSubjectKing), "king"),
               "the vocabulary resolves an identifier");
    test.check(pack.textFor(999999u) == nullptr, "an unnamed identifier is absent, not a failure");
}

/**
 * @brief One flipped byte, in the header, in the section table or in the last section, is refused.
 */
LPL_TEST(one_flipped_byte_is_refused)
{
    const lpl::core::u32 offsets[] = {40u, 64u, lpl::knowledge::kParityKnowledgeImageSize - 1u};

    for (const lpl::core::u32 offset : offsets)
    {
        alignas(16) lpl::core::u8 damaged[lpl::knowledge::kParityKnowledgeImageSize];
        lpl::knowledge::KnowledgePack broken;

        for (lpl::core::u32 index = 0u; index < lpl::knowledge::kParityKnowledgeImageSize; ++index)
            damaged[index] = lpl::knowledge::kParityKnowledgeImage[index];
        damaged[offset] = static_cast<lpl::core::u8>(damaged[offset] ^ 0xFFu);
        test.check(broken.open(damaged, lpl::knowledge::kParityKnowledgeImageSize) != lpl::knowledge::OpenStatus::Ok,
                   "one flipped byte is refused");
    }
}

/**
 * @brief A vocabulary whose text does not end in a NUL is refused for that, not for its hash:
 *        textFor returns a pointer into that block as a C string, so an unterminated block makes
 *        a lookup read past the section. The embedded image, whose text does terminate, opens.
 */
LPL_TEST(unterminated_vocabulary_text_is_refused)
{
    alignas(16) lpl::core::u8 image[lpl::knowledge::kParityKnowledgeImageSize];
    for (lpl::core::u32 index = 0u; index < lpl::knowledge::kParityKnowledgeImageSize; ++index)
        image[index] = lpl::knowledge::kParityKnowledgeImage[index];

    lpl::knowledge::KnowledgePack located;
    const lpl::core::u8 *section = nullptr;
    lpl::core::u32 sectionSize = 0u;
    if (!test.check(located.open(image, lpl::knowledge::kParityKnowledgeImageSize) == lpl::knowledge::OpenStatus::Ok &&
                        located.section(lpl::knowledge::SectionType::Vocabulary, section, sectionSize),
                    "the embedded image carries a vocabulary section"))
        return;

    const lpl::core::u32 lastByte = static_cast<lpl::core::u32>(section - image) + sectionSize - 1u;
    test.check(image[lastByte] == 0u, "its text ends in a NUL, as the format requires");
    image[lastByte] = static_cast<lpl::core::u8>('x');

    lpl::core::u32 hash = lpl::knowledge::kFnv1aOffsetBasis;
    lpl::knowledge::foldBytes(hash, image + sizeof(lpl::knowledge::Header),
                              lpl::knowledge::kParityKnowledgeImageSize -
                                  static_cast<lpl::core::u32>(sizeof(lpl::knowledge::Header)));
    for (lpl::core::u32 byte = 0u; byte < 4u; ++byte)
        image[offsetof(lpl::knowledge::Header, contentHash) + byte] =
            static_cast<lpl::core::u8>((hash >> (byte * 8u)) & 0xFFu);

    lpl::knowledge::KnowledgePack broken;
    test.check(broken.open(image, lpl::knowledge::kParityKnowledgeImageSize) ==
                   lpl::knowledge::OpenStatus::UnterminatedText,
               "the unterminated vocabulary text is refused, and not the hash");
}

/**
 * @brief Every claim of the image can be weighed: each has a source, nothing dangles, and a claim
 *        without a locus is counted rather than faulted.
 */
LPL_TEST(every_claim_can_be_weighed)
{
    lpl::knowledge::KnowledgePack pack;
    lpl::knowledge::ProvenanceAudit audit{};

    test.check(openEmbeddedImage(pack) == lpl::knowledge::OpenStatus::Ok, "the embedded image opens");
    test.check(lpl::knowledge::auditProvenance(pack, audit), "every claim can be weighed");
    test.check(audit.facts == 5u, "the audit saw every claim");
    test.check(audit.unsourced == 0u, "no claim is unsourced");
    test.check(audit.danglingLocus == 0u && audit.danglingDocument == 0u, "nothing dangles");
    test.check(audit.unlocated == 4u, "claims without a locus are counted, not faulted");
}

/**
 * @brief The one claim with a locus resolves to a citation that renders the passage the writer
 *        recorded and names its source.
 */
LPL_TEST(the_cited_claim_renders_its_passage)
{
    lpl::knowledge::KnowledgePack pack;
    lpl::knowledge::FactV1 cited{};
    bool found = false;

    test.check(openEmbeddedImage(pack) == lpl::knowledge::OpenStatus::Ok, "the embedded image opens");
    for (lpl::core::u32 index = 0u; !found && index < pack.factCount(); ++index)
    {
        lpl::knowledge::FactV1 fact{};

        found = pack.factAt(index, fact) && fact.locus != lpl::knowledge::kNoIdentifier;
        if (found)
            cited = fact;
    }
    test.check(found, "the cited claim is there");

    lpl::knowledge::Citation citation{};
    char rendered[192] = {};

    test.check(found && lpl::knowledge::cite(pack, cited, citation), "it resolves to a citation");
    test.check(citation.hasDocument == 1u && citation.hasLocus == 1u && citation.sourceName != nullptr,
               "with a source, a document and a locus");
    test.check(lpl::knowledge::renderCitation(citation, rendered, 192u) > 0u && contains(rendered, "3.12.412"),
               "and it renders the passage the writer recorded");
    test.check(contains(rendered, "chronicler"), "naming the source");
}

/**
 * @brief The image decodes, with nothing refused, into the corpus gate P13 history authors, in its
 *        canonical order.
 */
LPL_TEST(the_decoded_corpus_is_the_authored_corpus)
{
    lpl::knowledge::KnowledgePack pack;
    lpl::history::Corpus decoded;
    lpl::history::Corpus authored;
    lpl::knowledge::DecodeReport report{};

    test.check(openEmbeddedImage(pack) == lpl::knowledge::OpenStatus::Ok, "the embedded image opens");
    test.check(lpl::knowledge::toHistoryCorpus(pack, decoded, report), "the image decodes with nothing rejected");
    test.check(report.badKind == 0u && report.badWindow == 0u && report.badConfidence == 0u, "no record was refused");
    lpl::history::parityCorpus(authored);
    test.check(lpl::knowledge::corporaMatch(decoded, authored), "the decoded corpus is the authored corpus");

    bool sameOrder = (decoded.facts.size() == authored.facts.size());

    for (lpl::core::usize index = 0u; sameOrder && index < decoded.facts.size(); ++index)
        sameOrder = decoded.facts[index].source == authored.facts[index].source &&
                    decoded.facts[index].object == authored.facts[index].object;
    test.check(sameOrder, "the canonical corpus is authored in canonical order");
}

/**
 * @brief The decoder refuses what history cannot represent instead of clamping it: a confidence
 *        above one, a reversed window, a source kind it does not know.
 */
LPL_TEST(the_decoder_refuses_what_history_cannot_hold)
{
    lpl::knowledge::FactV1 wire{};
    lpl::history::Fact fact;

    wire.confidenceRaw = 65537u;
    test.check(!lpl::knowledge::fromWireFact(wire, fact), "a confidence above one is refused, not clamped");
    wire.confidenceRaw = 32768u;
    wire.fromDay = 1300;
    wire.toDay = 1200;
    test.check(!lpl::knowledge::fromWireFact(wire, fact), "a reversed window is refused");

    lpl::knowledge::SourceV1 source{};
    lpl::history::SourceProfile profile;

    source.kind = static_cast<lpl::core::u32>(lpl::knowledge::SourceKindV1::Count);
    test.check(!lpl::knowledge::fromWireSource(source, profile), "an unknown source kind is refused");
}

/**
 * @brief Gate P18 corpus: the canonical corpus, written to bytes by the writer and read back here,
 *        gives the history gate P13 builds from the corpus in memory, bit for bit, on both targets.
 *        A format can open cleanly and have rounded one confidence, and then the king died of
 *        something else.
 */
LPL_TEST(the_corpus_survives_the_round_trip_to_bytes)
{
    lpl::knowledge::KnowledgeFoldResult folded{};
    lpl::history::HistoryFoldResult history{};

    lpl::knowledge::foldKnowledgeState(lpl::knowledge::kParityKnowledgeImage, lpl::knowledge::kParityKnowledgeImageSize,
                                       folded);
    lpl::history::foldHistoryState(history);
    test.check(folded.openStatus == 0u, "the fold accepted the image");
    test.check(folded.roundTrip == 1u, "it round-tripped");
    test.check(folded.decodeRejected == 0u, "it rejected nothing");
    test.check(folded.provenanceOk == 1u, "provenance is sound");
    test.check(folded.queryMatched == 2u, "the canonical query found both contradictory claims");
    test.check(folded.queryTruncated == 0u, "and did not have to truncate");
    test.check(folded.consensusObject == lpl::history::kObjectDysentery, "the consensus is the osteologist's");
    test.check(folded.timelineSignature == history.timelineSignature, "its timeline is gate P13's");
    test.check(folded.chronicleSignature == history.chronicleSignature, "its chronicle is gate P13's");
    test.check(folded.minoritySignature == history.minoritySignature, "its minority world is gate P13's");
    test.check(folded.timelineSignature != 0u && folded.imageSignature != 0u && folded.factSignature != 0u &&
                   folded.vocabularySignature != 0u && folded.pageSignature != 0u && folded.citationSignature != 0u,
               "the signatures are not trivially zero");

    test.measureHexadecimal("image_signature", folded.imageSignature);
    test.measureHexadecimal("fact_signature", folded.factSignature);
    test.measureHexadecimal("vocabulary_signature", folded.vocabularySignature);
    test.measureHexadecimal("audit_signature", folded.auditSignature);
    test.measureHexadecimal("page_signature", folded.pageSignature);
    test.measureHexadecimal("citation_signature", folded.citationSignature);
    test.measureHexadecimal("timeline_signature", folded.timelineSignature);
    test.measureHexadecimal("chronicle_signature", folded.chronicleSignature);
    test.measureHexadecimal("minority_signature", folded.minoritySignature);
    test.measure("image_bytes", folded.imageBytes);
    test.measure("sections", folded.sections);
    test.measure("skipped", folded.skipped);
    test.measure("facts", folded.facts);
    test.measure("sources", folded.sources);
    test.measure("documents", folded.documents);
    test.measure("loci", folded.loci);
    test.measure("vocabulary", folded.vocabulary);
    test.measure("query_returned", folded.queryReturned);
}
