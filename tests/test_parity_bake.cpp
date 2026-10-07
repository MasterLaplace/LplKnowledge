#include <lpl/harvest/Baker.hpp>
#include <lpl/knowledge/ParityKnowBlob.hpp>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int gChecks = 0;
int gFailures = 0;

/**
 * @brief Records one check, and prints it when it does not hold.
 *
 * @param label What was checked.
 * @param ok    Whether it held.
 */
void check(const char *label, bool ok)
{
    ++gChecks;
    if (!ok)
    {
        ++gFailures;
        std::printf("  (fail) %s\n", label);
    }
}

/**
 * @brief The canonical corpus bakes whole, the same bytes twice, and those bytes are the embedded
 *        image: a corpus or a writer that changed without `lpl-knowbake --header` being run again
 *        leaves the kernel folding a stale image.
 */
void checkTheCanonicalBake()
{
    std::vector<lpl::core::u8> image;
    lpl::harvest::BakeReport bake{};

    check("the canonical corpus bakes", lpl::harvest::bakeParityCorpus(image, bake));
    check("it wrote every claim the corpus holds", bake.facts == 5u);
    check("and every source", bake.sources == 4u);
    check("one document and one locus", bake.documents == 1u && bake.loci == 1u);
    check("no collision", bake.collisions == 0u);
    check("no unsourced claim", bake.unsourced == 0u);
    check("the image is not empty", bake.bytes > 0u && bake.bytes == image.size());

    std::vector<lpl::core::u8> again;
    lpl::harvest::BakeReport twice{};

    check("baking twice gives the same bytes", lpl::harvest::bakeParityCorpus(again, twice) && again == image);
    check("the embedded image is the one the writer bakes",
          image.size() == lpl::knowledge::kParityKnowledgeImageSize &&
              std::memcmp(image.data(), lpl::knowledge::kParityKnowledgeImage, image.size()) == 0);
}

/**
 * @brief The writer refuses an identifier named with two different words, and a claim whose source
 *        nothing describes.
 */
void checkTheWriterRefusals()
{
    lpl::harvest::Baker collider;
    std::vector<lpl::core::u8> discarded;
    lpl::harvest::BakeReport refused{};

    check("naming an identifier twice with the same word is idempotent",
          collider.name(42u, "king") && collider.name(42u, "king"));
    check("naming it with a different word is refused", !collider.name(42u, "queen"));
    check("and the bake is refused", !collider.build(discarded, refused));
    check("with the collision reported", refused.collisions == 1u && !refused.firstCollision.empty());

    lpl::harvest::Baker orphan;
    lpl::knowledge::FactV1 fact{};
    lpl::harvest::BakeReport orphanReport{};

    fact.subject = 1u;
    fact.predicate = 2u;
    fact.object = 3u;
    fact.source = 777u;
    fact.confidenceRaw = 32768u;
    orphan.addFact(fact);
    check("a claim whose source nothing describes is refused", !orphan.build(discarded, orphanReport));
    check("and counted", orphanReport.unsourced == 1u);
}

} // namespace

/**
 * @brief The writer's half of gate P18 corpus: the canonical corpus bakes, the same bytes every
 *        time, and those bytes are the image the kernel embeds.
 *
 * @details The reader's half, the fold and its round trip, is `tests/knowledge/corpus.cpp`: an
 *          LPL_TEST that runs on the host and in ring 0. The writer is hosted, so its claims stay
 *          here.
 */
int main()
{
    std::printf("test-parity-bake: the writer's half of gate P18 corpus\n");
    checkTheCanonicalBake();
    checkTheWriterRefusals();
    std::printf("\n%s (%d failures, %d checks)\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
