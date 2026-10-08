/**
 * @file test_entity_resolution.cpp
 * @brief Two names, one thing — and the far more dangerous converse.
 *
 * The errors this module can make are invisible by construction: a corpus that has fused two
 * people does not look damaged, it looks confident. So the checks here come in pairs, and
 * the second of each pair is the one that matters. It is not enough that a corpus built to
 * corroborate does; a corpus built NOT to corroborate has to fail to, or the measurement is
 * satisfied by a function that says yes to everything.
 *
 * Every number here is an integer in raw Q16.16, so nothing checked needs Fixed32. It is built
 * with the foundation only because harvest/ is: Baker.hpp lays relief out in lpl::math's
 * projection.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include <lpl/harvest/EntityResolution.hpp>
#include <lpl/harvest/Baker.hpp>
#include <lpl/knowledge/KnowledgePack.hpp>

#include <set>
#include <utility>

#include <cstdio>
#include <string>
#include <vector>

namespace {

int gChecks = 0;
int gFailures = 0;

/**
 * @brief Records one check.
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
 * @brief The representative a mention was given.
 *
 * @param clustering The result.
 * @param index      Which mention, by input position.
 * @return Its cluster label.
 */
[[nodiscard]] lpl::core::u32 labelOf(const lpl::harvest::Clustering &clustering, std::size_t index)
{
    return index < clustering.representative.size() ? clustering.representative[index] : 0u;
}

} // namespace

int main()
{
    std::printf("test-entity-resolution — one witness is one witness\n");

    // ── Normalising an address ────────────────────────────────────────────────
    std::printf("── addresses\n");
    using lpl::harvest::normaliseReference;

    check("scheme and case do not distinguish an address",
          normaliseReference("HTTPS://Example.ORG/paper") == normaliseReference("http://example.org/paper"));
    check("a leading www does not either",
          normaliseReference("https://www.example.org/paper") == normaliseReference("https://example.org/paper"));
    check("a trailing slash does not either",
          normaliseReference("https://example.org/paper/") == normaliseReference("https://example.org/paper"));
    check("a fragment does not either",
          normaliseReference("https://example.org/paper#results") == normaliseReference("https://example.org/paper"));
    check("tracking parameters do not either",
          normaliseReference("https://example.org/p?utm_source=x") == normaliseReference("https://example.org/p"));

    // A DOI however it is dressed. Three spellings of one work, and a search provider will
    // hand back all three across a few runs.
    const std::string doi = normaliseReference("https://doi.org/10.1038/s41467-018-04252-2");
    check("a resolver-prefixed DOI reduces to the DOI",
          doi == normaliseReference("http://dx.doi.org/10.1038/s41467-018-04252-2"));
    check("a bare DOI reduces to the same thing", doi == normaliseReference("10.1038/s41467-018-04252-2"));
    check("and it is marked as a DOI rather than a host path", doi.rfind("doi:", 0u) == 0u);

    // arXiv: the case that actually appeared in a real run's source list.
    const std::string ax = normaliseReference("http://arxiv.org/abs/2101.01303v1");
    check("an arXiv abstract and its PDF are one work", ax == normaliseReference("https://arxiv.org/pdf/2101.01303"));
    check("so are two versions of it", ax == normaliseReference("arxiv.org/abs/2101.01303v3"));
    check("and it is marked as arXiv", ax == "arxiv:2101.01303");

    // @warning The converse, and it is the check that stops the rules above from being a
    // sledgehammer: two genuinely different papers must NOT collapse.
    check("two different DOIs stay different",
          normaliseReference("10.1038/a") != normaliseReference("10.1038/b"));
    check("two different arXiv identifiers stay different",
          normaliseReference("arxiv.org/abs/2101.01303") != normaliseReference("arxiv.org/abs/2101.01304"));
    check("a meaningful query string is kept",
          normaliseReference("https://example.org/view?id=7") != normaliseReference("https://example.org/view?id=8"));
    check("an empty address normalises to nothing", normaliseReference("").empty());

    // ── Resemblance ───────────────────────────────────────────────────────────
    std::printf("── resemblance\n");
    using lpl::harvest::kSimilarityOne;
    using lpl::harvest::nameSimilarity;

    check("a name is identical to itself", nameSimilarity("Louis XIV", "Louis XIV") == kSimilarityOne);
    check("word order does not matter", nameSimilarity("Louis XIV", "XIV Louis") == kSimilarityOne);
    check("case and punctuation do not matter", nameSimilarity("Louis-XIV", "louis xiv") == kSimilarityOne);
    check("an added qualifier is close but not equal",
          nameSimilarity("Louis XIV", "Louis XIV of France") > 0u &&
              nameSimilarity("Louis XIV", "Louis XIV of France") < kSimilarityOne);
    check("unrelated names score nothing", nameSimilarity("Louis XIV", "Ada Lovelace") == 0u);

    // @warning The honest failure, asserted rather than tuned away. Nothing about these two strings
    // says they are one man, and a metric that claimed otherwise would be guessing.
    check("a nickname and a regnal name are NOT alike as strings",
          nameSimilarity("The Sun King", "Louis XIV") == 0u);

    // The near-miss that a character-based metric gets backwards: these two are one letter
    // apart and are two different towns.
    check("two towns one letter apart do not resemble each other",
          nameSimilarity("Jean Martin Rouen", "Jean Martin Rennes") < kSimilarityOne);

    check("an initialism is recognised", lpl::harvest::isAbbreviationOf("WHO", "World Health Organization"));
    check("but not out of order", !lpl::harvest::isAbbreviationOf("WHO", "Oxford Hospital Ward"));
    check("and a single letter is not an abbreviation", !lpl::harvest::isAbbreviationOf("W", "World"));

    // ── Clustering ────────────────────────────────────────────────────────────
    std::printf("── clustering\n");
    lpl::harvest::ResolutionParams params;
    lpl::harvest::Clustering clustering;

    {
        std::vector<lpl::harvest::Mention> mentions{
            {1u, "A deterministic replay study", "https://doi.org/10.1/x"},
            {2u, "Deterministic replay (preprint)", "http://dx.doi.org/10.1/x"},
            {3u, "Something else entirely", "https://doi.org/10.1/y"},
        };
        check("the batch resolves", lpl::harvest::resolveEntities(mentions, params, clustering));
        check("two spellings of one address are one thing",
              labelOf(clustering, 0u) == labelOf(clustering, 1u));
        check("a different address is a different thing",
              labelOf(clustering, 2u) != labelOf(clustering, 0u));
        check("and the batch says how many things it found", clustering.clusters == 2u);
        check("the representative is the smallest member id", labelOf(clustering, 1u) == 1u);
    }

    // @warning The rule the whole file turns on: resemblance PROPOSES, it does not merge. Two
    // homonymous notaries, and a third spelling sitting between them — the exact shape that
    // makes a threshold-merger fuse two men into one who was in two places at once.
    {
        std::vector<lpl::harvest::Mention> mentions{
            {1u, "Jean Martin notary at Rouen", ""},
            {2u, "Jean Martin notary", ""},
            {3u, "Jean Martin notary at Rennes", ""},
        };
        check("the batch resolves", lpl::harvest::resolveEntities(mentions, params, clustering));
        check("nothing was merged on resemblance alone", clustering.merged == 0u);
        check("the two notaries are still two men", labelOf(clustering, 0u) != labelOf(clustering, 2u));
        check("but the resemblance was reported", !clustering.candidates.empty());
        check("candidates arrive strongest first",
              clustering.candidates.size() < 2u ||
                  clustering.candidates[0].score >= clustering.candidates[1].score);

        // And the opt-in does what it says, so the default being off is a choice rather than
        // a missing feature.
        lpl::harvest::ResolutionParams eager;
        eager.autoMergeThreshold = 32768u; // 0.5
        lpl::harvest::Clustering fused;
        check("the batch resolves again", lpl::harvest::resolveEntities(mentions, eager, fused));
        check("opting in does merge them", fused.autoMerged > 0u);
        check("which is exactly the error the default prevents", fused.clusters < clustering.clusters);
    }

    {
        std::vector<lpl::harvest::Mention> duplicated{{7u, "a", ""}, {7u, "b", ""}};
        check("a repeated handle is refused rather than silently collapsed",
              !lpl::harvest::resolveEntities(duplicated, params, clustering));
    }

    // ── Independence: the number `fuseConfidence` cannot check for itself ─────
    std::printf("── independence\n");
    using lpl::harvest::AgreementReport;
    using lpl::harvest::Derivation;
    using lpl::harvest::Testimony;

    // (a) A corpus built TO corroborate. Two genuinely different papers, one shared claim.
    {
        const std::vector<lpl::core::u32> sources{10u, 20u};
        const std::vector<std::string> references{"https://doi.org/10.1/a", "https://doi.org/10.2/b"};
        const std::vector<Testimony> testimonies{{100u, 10u}, {100u, 20u}};
        AgreementReport report{};
        check("the corpus is counted", lpl::harvest::countIndependentAgreements(sources, references, testimonies,
                                                                               {}, report));
        check("two papers are two witnesses", report.witnesses == 2u);
        check("the shared claim is corroborated", report.corroborated == 1u);
        check("each paper has one other witness behind it",
              report.agreements.size() == 2u && report.agreements[0] == 1u && report.agreements[1] == 1u);
        check("nothing was inflated", report.inflated == 0u);
    }

    // (b) @warning The SAME corpus built NOT to corroborate: two rows, one work, one claim. This is
    //     the check that makes (a) mean something. A counter that simply counts rows passes
    //     (a) and fails here, and a corpus fused this way reads as doubly attested while
    //     resting on a single document.
    {
        const std::vector<lpl::core::u32> sources{10u, 20u};
        const std::vector<std::string> references{"https://doi.org/10.1/a", "http://dx.doi.org/10.1/a"};
        const std::vector<Testimony> testimonies{{100u, 10u}, {100u, 20u}};
        AgreementReport report{};
        check("the corpus is counted", lpl::harvest::countIndependentAgreements(sources, references, testimonies,
                                                                               {}, report));
        check("one work cited twice is ONE witness", report.witnesses == 1u);
        check("the second row collapsed into the first", report.collapsed == 1u);
        check("so nothing is corroborated", report.corroborated == 0u);
        check("neither source may claim an agreement",
              report.agreements.size() == 2u && report.agreements[0] == 0u && report.agreements[1] == 0u);
        check("and the corpus says it was going to look otherwise", report.inflated == 1u);
    }

    // (c) A declared derivation: two different addresses, but one copied the other. Nothing
    //     about the addresses could reveal this, which is why it is declared and not guessed.
    {
        const std::vector<lpl::core::u32> sources{10u, 20u};
        const std::vector<std::string> references{"https://a.example/chronicle", "https://b.example/copy"};
        const std::vector<Testimony> testimonies{{100u, 10u}, {100u, 20u}};
        const std::vector<Derivation> derivations{{20u, 10u}};
        AgreementReport report{};
        check("the corpus is counted",
              lpl::harvest::countIndependentAgreements(sources, references, testimonies, derivations, report));
        check("two chroniclers copying one original are one witness", report.witnesses == 1u);
        check("so the claim is not corroborated", report.corroborated == 0u);
        check("and the appearance of agreement is reported", report.inflated == 1u);

        // The control on the control: drop the derivation and the same corpus corroborates.
        // Without this, "not corroborated" could be coming from anywhere.
        AgreementReport independent{};
        check("the corpus is counted without the derivation",
              lpl::harvest::countIndependentAgreements(sources, references, testimonies, {}, independent));
        check("with no derivation declared they are two witnesses", independent.witnesses == 2u);
        check("and the claim is corroborated", independent.corroborated == 1u);
    }

    // (d) An unaddressed source is not equal to another unaddressed source. Otherwise every
    //     source a harvester failed to address would fuse into one, quietly.
    {
        const std::vector<lpl::core::u32> sources{10u, 20u};
        const std::vector<std::string> references{"", ""};
        const std::vector<Testimony> testimonies{{100u, 10u}, {100u, 20u}};
        AgreementReport report{};
        check("the corpus is counted", lpl::harvest::countIndependentAgreements(sources, references, testimonies,
                                                                               {}, report));
        check("two unaddressed sources are two witnesses", report.witnesses == 2u);
    }

    // (e) Malformed input is refused rather than half-counted.
    {
        AgreementReport report{};
        check("a mismatched reference list is refused",
              !lpl::harvest::countIndependentAgreements({1u, 2u}, {"a"}, {}, {}, report));
        check("a repeated source id is refused",
              !lpl::harvest::countIndependentAgreements({1u, 1u}, {"a", "b"}, {}, {}, report));
    }

    // ── Blocking: exact, or it is a heuristic wearing a proof's clothes ──────
    std::printf("── blocking\n");
    {
        // A corpus big enough that the quadratic sweep is visibly wasteful and small enough
        // that it can still be RUN — which is the whole point: the claim is that blocking
        // finds the same pairs, and the only way to check that is to compute both.
        constexpr std::size_t kCount = 1500u;
        static const char *const kFirst[] = {"Marcus", "Gaius", "Lucius", "Publius", "Titus",
                                             "Quintus", "Aulus", "Sextus"};
        static const char *const kFamily[] = {"Tullius", "Julius", "Cornelius", "Claudius",
                                              "Valerius", "Aurelius", "Flavius", "Domitius"};
        static const char *const kName[] = {"Cicero", "Caesar", "Scipio", "Nero", "Maximus",
                                            "Rufus", "Cato", "Brutus", "Crassus", "Varro"};

        std::vector<lpl::harvest::Mention> crowd;
        crowd.reserve(kCount);
        lpl::core::u32 state = 12345u;
        for (std::size_t i = 0u; i < kCount; ++i)
        {
            // A deterministic generator, so the measurement below is the same on two machines.
            state = state * 1664525u + 1013904223u;
            std::string name = kFirst[(state >> 5) % 8u];
            name += " ";
            name += kFamily[(state >> 11) % 8u];
            name += " ";
            name += kName[(state >> 17) % 10u];
            // A third of them carry an extra token, so the corpus holds real near-duplicates
            // rather than only exact repeats.
            if ((state >> 23) % 3u == 0u)
                name += " Minor";
            crowd.push_back(lpl::harvest::Mention{static_cast<lpl::core::u32>(i + 1u), name, ""});
        }

        lpl::harvest::Clustering blocked;
        check("the crowd resolves", lpl::harvest::resolveEntities(crowd, params, blocked));

        // The exhaustive answer, computed here rather than trusted: every pair, scored.
        std::set<std::pair<lpl::core::u32, lpl::core::u32>> exhaustive;
        std::size_t swept = 0u;
        for (std::size_t i = 0u; i < crowd.size(); ++i)
        {
            for (std::size_t j = i + 1u; j < crowd.size(); ++j)
            {
                ++swept;
                const lpl::core::u32 score =
                    lpl::harvest::nameSimilarity(crowd[i].name, crowd[j].name);
                if (score >= params.proposeThreshold)
                    exhaustive.emplace(crowd[i].id, crowd[j].id);
            }
        }

        std::set<std::pair<lpl::core::u32, lpl::core::u32>> found;
        for (const lpl::harvest::Candidate &candidate : blocked.candidates)
            if (candidate.evidence == lpl::harvest::Evidence::SimilarName)
                found.emplace(candidate.left, candidate.right);

        // @warning The claim. Prefix filtering is exact for Jaccard, so this is an EQUALITY and not
        // a recall figure. A blocking scheme that loses pairs is a blocking scheme that
        // silently stops finding duplicates as a corpus grows — the failure nobody notices.
        check("blocking finds every pair the exhaustive sweep finds", found == exhaustive);
        check("and invents none it does not", found.size() == exhaustive.size());
        check("the corpus really does contain pairs to find", !exhaustive.empty());

        // And it is cheaper, measured rather than asserted.
        check("and it scored far fewer pairs to do it",
              blocked.comparisons < static_cast<lpl::core::u32>(swept / 4u));
        std::printf("     %zu mentions: %zu pairs swept exhaustively, %u scored after blocking "
                    "(%u blocks, %zu pairs found)\n",
                    kCount, swept, blocked.comparisons, blocked.blocks, exhaustive.size());
    }

    // The cap that used to sit at 4096 is gone, and this is what says so: a corpus larger
    // than it still proposes. Under the old sweep this returned nothing at all.
    {
        std::vector<lpl::harvest::Mention> many;
        many.reserve(5000u);
        for (lpl::core::u32 i = 0u; i < 5000u; ++i)
            many.push_back({i + 1u, "Herodotus Histories book " + std::to_string(i % 9u), ""});
        lpl::harvest::Clustering big;
        check("a corpus past the old 4096 cap still resolves",
              lpl::harvest::resolveEntities(many, params, big));
        check("and still proposes pairs", !big.candidates.empty());
    }

    std::printf("-- a suspicion survives the run\n");
    {
        // @warning **The whole point of the section, and it did not exist until now.** `Clustering`
        // computes candidates and says in its own comment that they are "alike, not merged" --
        // and then every run threw them away. Leaving two records apart is the safe default,
        // because splitting is reversible and merging is not; but leaving them apart AND
        // forgetting they resembled each other means corroboration can never happen, and one man
        // with forty sources stays forty men with one source each.
        lpl::harvest::Baker baker;
        check("the corpus names its subject", baker.name(1u, "Martin, Jean (Rouen)"));
        check("and the other one", baker.name(2u, "Martin, Jean (Rennes)"));
        baker.addCandidate(2u, 1u, 45875u, static_cast<lpl::core::u32>(lpl::harvest::Evidence::SimilarName));
        check("one suspicion is held", baker.candidateCount() == 1u);

        std::vector<lpl::core::u8> image;
        lpl::harvest::BakeReport report{};
        check("it bakes", baker.build(image, report));

        lpl::knowledge::KnowledgePack pack;
        check("and reopens", pack.open(image.data(), image.size()) == lpl::knowledge::OpenStatus::Ok);
        check("carrying the pair", pack.candidateCount() == 1u);

        lpl::knowledge::CandidateV1 wire{};
        check("which reads back", pack.candidateAt(0u, wire));
        // @warning Smaller identifier first, whichever order the caller asked in. One question must
        // have one spelling, or the same suspicion recorded twice would look like two independent
        // ones -- which is exactly the miscount corroboration must never make.
        check("with the pair spelled one way", wire.left == 1u && wire.right == 2u);
        check("and the resemblance kept as a raw word", wire.scoreRaw == 45875u);
        check("and what suggested it",
              wire.evidence == static_cast<lpl::core::u32>(lpl::harvest::Evidence::SimilarName));

        // @warning A candidate is NOT a merge, and nothing here may read like one: no fact was
        // written, and the two names remain two names. It records that somebody should look --
        // the question a specialist can answer and a hard key can settle.
        check("nothing was merged by recording it", pack.factCount() == 0u);
    }

    std::printf("\n%s (%d failures, %d checks)\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
