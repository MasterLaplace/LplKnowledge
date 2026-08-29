/**
 * @file EntityResolution.cpp
 * @brief Implementation of deciding that two names denote one thing.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/EntityResolution.hpp>

#include <algorithm>
#include <map>
#include <set>

namespace lpl::harvest {

namespace {

/**
 * @brief Lower-cases an ASCII byte.
 *
 * ASCII only, and the limit is worth naming: a name written 'Étienne' and one written
 * 'ETIENNE' will not fold together here. Folding accents correctly is a Unicode table, and
 * a half-done one that handles French and mangles Greek would be worse than none in a
 * repository whose corpora are meant to include both.
 *
 * @param c The byte.
 * @return Its lower-case form when it is an ASCII letter, otherwise itself.
 */
[[nodiscard]] char lowered(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/**
 * @brief Is this byte part of a word?
 *
 * @param c The byte.
 * @return true for ASCII letters and digits.
 */
[[nodiscard]] bool wordByte(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/**
 * @brief Splits a name into lower-cased alphanumeric tokens.
 *
 * A SET, so a name repeating a word does not out-weigh one that does not. Bounded, because
 * the input is a harvested string: a pathological name must cost a bounded amount of work
 * rather than however much it asks for.
 *
 * @param text The name.
 * @return Its tokens, sorted and unique.
 */
[[nodiscard]] std::vector<std::string> tokenSet(std::string_view text)
{
    constexpr std::size_t kMaxTokens = 64u;
    std::vector<std::string> tokens;
    std::size_t i = 0u;
    while (i < text.size() && tokens.size() < kMaxTokens)
    {
        while (i < text.size() && !wordByte(text[i]))
            ++i;
        const std::size_t start = i;
        while (i < text.size() && wordByte(text[i]))
            ++i;
        if (i > start)
        {
            std::string token;
            token.reserve(i - start);
            for (std::size_t k = start; k < i; ++k)
                token.push_back(lowered(text[k]));
            tokens.push_back(std::move(token));
        }
    }
    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
    return tokens;
}

/**
 * @brief Strips a leading scheme.
 *
 * @param text The address, modified in place.
 */
void dropScheme(std::string &text)
{
    const std::size_t at = text.find("://");
    if (at != std::string::npos && at <= 8u)
        text.erase(0u, at + 3u);
}

/**
 * @brief Removes the query parameters that identify a click rather than a document.
 *
 * @param text The address, modified in place.
 */
void dropTracking(std::string &text)
{
    static const char *const kTracking[] = {"utm_", "fbclid", "gclid", "ref=", "source="};
    const std::size_t question = text.find('?');
    if (question == std::string::npos)
        return;

    const std::string query = text.substr(question + 1u);
    std::string kept;
    std::size_t cursor = 0u;
    while (cursor <= query.size())
    {
        const std::size_t amp = query.find('&', cursor);
        const std::string part = query.substr(cursor, (amp == std::string::npos ? query.size() : amp) - cursor);
        cursor = (amp == std::string::npos) ? query.size() + 1u : amp + 1u;
        if (part.empty())
            continue;
        bool tracking = false;
        for (const char *marker : kTracking)
            if (part.rfind(marker, 0u) == 0u)
                tracking = true;
        if (!tracking)
            kept += (kept.empty() ? "" : "&") + part;
    }
    text.erase(question);
    if (!kept.empty())
        text += "?" + kept;
}

} // namespace

void blockingPrefix(std::string_view name, const std::vector<std::pair<std::string, core::u32>> &frequency,
                    core::u32 threshold, std::vector<std::string> &out)
{
    out.clear();
    std::vector<std::string> tokens = tokenSet(name);
    if (tokens.empty())
        return;

    // Rarest first. Ties broken by the token itself, because two machines have to order a
    // corpus the same way or they index different prefixes and find different pairs.
    std::sort(tokens.begin(), tokens.end(), [&frequency](const std::string &a, const std::string &b) {
        const auto find = [&frequency](const std::string &t) -> core::u32 {
            const auto at = std::lower_bound(frequency.begin(), frequency.end(), t,
                                             [](const std::pair<std::string, core::u32> &e,
                                                const std::string &v) { return e.first < v; });
            return (at != frequency.end() && at->first == t) ? at->second : 0u;
        };
        const core::u32 fa = find(a);
        const core::u32 fb = find(b);
        return fa != fb ? fa < fb : a < b;
    });

    // |a| - ceil(t*|a|) + 1, computed in integers. At t = 0 every token is indexed, which is
    // correct: with no floor, any shared token at all can matter.
    const core::u64 size = tokens.size();
    const core::u64 needed = (static_cast<core::u64>(threshold) * size + kSimilarityOne - 1u) / kSimilarityOne;
    core::u64 prefix = size >= needed ? size - needed + 1u : size;
    if (prefix > size)
        prefix = size;
    if (prefix == 0u)
        prefix = 1u;

    out.assign(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(prefix));
}

std::string normaliseReference(std::string_view reference)
{
    std::string text{reference};

    // Trim, then drop what never distinguishes two spellings of one address.
    while (!text.empty() && (text.front() == ' ' || text.front() == '<'))
        text.erase(text.begin());
    while (!text.empty() && (text.back() == ' ' || text.back() == '>' || text.back() == '/' || text.back() == '.'))
        text.pop_back();
    if (text.empty())
        return {};

    const std::size_t hash = text.find('#');
    if (hash != std::string::npos)
        text.erase(hash);

    dropScheme(text);
    dropTracking(text);

    for (char &c : text)
        c = lowered(c);

    if (text.rfind("www.", 0u) == 0u)
        text.erase(0u, 4u);

    // A DOI, however it is dressed. `10.` at the head of what remains is a bare DOI, which
    // is how they are written in a bibliography.
    for (const char *host : {"doi.org/", "dx.doi.org/"})
    {
        if (text.rfind(host, 0u) == 0u)
        {
            text.erase(0u, std::char_traits<char>::length(host));
            break;
        }
    }
    if (text.rfind("10.", 0u) == 0u && text.find('/') != std::string::npos)
        return "doi:" + text;

    // arXiv's several faces. The version suffix goes: versions of a preprint are one work,
    // and keeping them apart would count one author agreeing with themselves.
    if (text.rfind("arxiv.org/", 0u) == 0u)
    {
        std::string rest = text.substr(std::char_traits<char>::length("arxiv.org/"));
        for (const char *form : {"abs/", "pdf/"})
            if (rest.rfind(form, 0u) == 0u)
                rest.erase(0u, std::char_traits<char>::length(form));
        if (rest.size() > 4u && rest.substr(rest.size() - 4u) == ".pdf")
            rest.erase(rest.size() - 4u);
        // A trailing `v<digits>`, and only that: an identifier ending in a letter followed
        // by digits is common, so the `v` must be the last non-digit for this to fire.
        std::size_t at = rest.size();
        while (at > 0u && rest[at - 1u] >= '0' && rest[at - 1u] <= '9')
            --at;
        if (at > 0u && at < rest.size() && rest[at - 1u] == 'v')
            rest.erase(at - 1u);
        if (!rest.empty())
            return "arxiv:" + rest;
    }

    return text;
}

core::u32 nameSimilarity(std::string_view left, std::string_view right) noexcept
{
    const std::vector<std::string> a = tokenSet(left);
    const std::vector<std::string> b = tokenSet(right);
    if (a.empty() || b.empty())
        return 0u;

    std::size_t shared = 0u;
    std::size_t i = 0u;
    std::size_t j = 0u;
    while (i < a.size() && j < b.size())
    {
        if (a[i] == b[j])
        {
            ++shared;
            ++i;
            ++j;
        }
        else if (a[i] < b[j])
            ++i;
        else
            ++j;
    }

    const std::size_t united = a.size() + b.size() - shared;
    if (united == 0u)
        return 0u;
    // Multiply before dividing, in 64 bits: the other order would floor the ratio to zero
    // for every pair, which is the shape of bug that reads as "nothing ever matches".
    return static_cast<core::u32>((static_cast<core::u64>(shared) * kSimilarityOne) / united);
}

bool isAbbreviationOf(std::string_view shortForm, std::string_view longForm) noexcept
{
    std::string letters;
    for (char c : shortForm)
        if (wordByte(c))
            letters.push_back(lowered(c));
    if (letters.size() < 2u)
        return false;

    const std::vector<std::string> words = tokenSet(longForm);
    if (words.size() < letters.size())
        return false;

    // In order, against the words as they were WRITTEN rather than the sorted set: an
    // initialism is a statement about sequence, and matching against a sorted set would call
    // 'WHO' an abbreviation of 'Oxford Hospital Ward'.
    std::vector<std::string> ordered;
    std::size_t i = 0u;
    while (i < longForm.size())
    {
        while (i < longForm.size() && !wordByte(longForm[i]))
            ++i;
        const std::size_t start = i;
        while (i < longForm.size() && wordByte(longForm[i]))
            ++i;
        if (i > start)
            ordered.push_back(std::string{longForm.substr(start, i - start)});
    }
    if (ordered.size() < letters.size())
        return false;

    std::size_t at = 0u;
    for (const std::string &word : ordered)
    {
        if (at < letters.size() && !word.empty() && lowered(word[0]) == letters[at])
            ++at;
    }
    return at == letters.size();
}

namespace {

/**
 * @class DisjointSet
 * @brief Union-find whose representative is always the smallest member.
 *
 * Smallest rather than "whichever was seen first": the same corpus has to resolve to the
 * same labels on two machines, and a representative chosen by insertion order would make
 * the output a fact about the walk rather than about the corpus.
 */
class DisjointSet {
public:
    /**
     * @brief Sizes the structure.
     *
     * @param count How many elements.
     */
    explicit DisjointSet(std::size_t count) : _parent(count)
    {
        for (std::size_t i = 0u; i < count; ++i)
            _parent[i] = i;
    }

    /**
     * @brief Finds an element's representative.
     *
     * @param x The element.
     * @return Its root.
     */
    [[nodiscard]] std::size_t find(std::size_t x)
    {
        while (_parent[x] != x)
        {
            _parent[x] = _parent[_parent[x]];
            x = _parent[x];
        }
        return x;
    }

    /**
     * @brief Merges two elements' sets.
     *
     * @param a One.
     * @param b The other.
     * @return true when they were not already together.
     */
    bool unite(std::size_t a, std::size_t b)
    {
        const std::size_t ra = find(a);
        const std::size_t rb = find(b);
        if (ra == rb)
            return false;
        // Toward the smaller index, which is what makes the representative deterministic.
        if (ra < rb)
            _parent[rb] = ra;
        else
            _parent[ra] = rb;
        return true;
    }

private:
    std::vector<std::size_t> _parent;
};

} // namespace

bool resolveEntities(const std::vector<Mention> &mentions, const ResolutionParams &params, Clustering &out)
{
    out = Clustering{};

    std::set<core::u32> seen;
    for (const Mention &mention : mentions)
        if (!seen.insert(mention.id).second)
            return false;

    const std::size_t count = mentions.size();
    out.representative.assign(count, 0u);
    if (count == 0u)
        return true;

    DisjointSet sets{count};

    // ── Hard evidence: equal normalised references ────────────────────────────
    std::map<std::string, std::size_t> byReference;
    for (std::size_t i = 0u; i < count; ++i)
    {
        const std::string key = normaliseReference(mentions[i].reference);
        if (key.empty())
            continue;
        const auto found = byReference.find(key);
        if (found == byReference.end())
            byReference[key] = i;
        else if (sets.unite(found->second, i))
            ++out.merged;
    }

    // ── Soft evidence: proposed, and merged only if the caller asked ──────────
    //
    // Blocked rather than swept. The sweep this replaces was quadratic and capped at 4096
    // mentions, which is a bound that silently stops proposing anything on a real corpus —
    // and a catalogue of the world's texts is millions of rows, where n(n-1)/2 is not a slow
    // answer but no answer at all.
    //
    // The blocking is EXACT: see @ref blockingPrefix. Two names cannot reach the propose
    // threshold without sharing a token in their prefixes, so nothing the sweep would have
    // found is lost — which `test-entity-resolution` asserts against the sweep rather than
    // arguing.
    {
        std::map<std::string, core::u32> counts;
        for (const Mention &mention : mentions)
            for (const std::string &token : tokenSet(mention.name))
                ++counts[token];
        std::vector<std::pair<std::string, core::u32>> frequency{counts.begin(), counts.end()};

        std::map<std::string, std::vector<std::size_t>> postings;
        std::vector<std::string> prefix;
        for (std::size_t i = 0u; i < count; ++i)
        {
            blockingPrefix(mentions[i].name, frequency, params.proposeThreshold, prefix);
            for (const std::string &token : prefix)
                postings[token].push_back(i);
        }
        out.blocks = static_cast<core::u32>(postings.size());

        // A pair may share several prefix tokens; scored once. Kept as a set of pairs rather
        // than a flag per mention because the same mention legitimately pairs with many.
        std::set<std::pair<std::size_t, std::size_t>> seenPairs;
        for (const auto &entry : postings)
        {
            const std::vector<std::size_t> &bucket = entry.second;
            for (std::size_t a = 0u; a < bucket.size(); ++a)
                for (std::size_t b = a + 1u; b < bucket.size(); ++b)
                    seenPairs.emplace(bucket[a], bucket[b]);
        }

        for (const auto &pair : seenPairs)
        {
            const std::size_t i = pair.first;
            const std::size_t j = pair.second;
            if (sets.find(i) == sets.find(j))
                continue; // already one thing on hard evidence; nothing to propose

            ++out.comparisons;
            const core::u32 score = nameSimilarity(mentions[i].name, mentions[j].name);
            Evidence evidence = Evidence::None;
            core::u32 reported = score;

            if (score >= params.proposeThreshold)
                evidence = Evidence::SimilarName;
            else if (isAbbreviationOf(mentions[i].name, mentions[j].name) ||
                     isAbbreviationOf(mentions[j].name, mentions[i].name))
            {
                evidence = Evidence::Abbreviation;
                reported = params.proposeThreshold;
            }

            if (evidence == Evidence::None)
                continue;

            if (params.autoMergeThreshold != 0u && score >= params.autoMergeThreshold)
            {
                if (sets.unite(i, j))
                {
                    ++out.merged;
                    ++out.autoMerged;
                }
                continue;
            }

            out.candidates.push_back(Candidate{mentions[i].id, mentions[j].id, reported, evidence});
        }

        // @warning An initialism shares no token with its expansion — that is exactly why
        // `isAbbreviationOf` exists — so blocking on tokens cannot find those pairs. Swept
        // separately, and only over names short enough to be an initialism, which keeps the
        // cost proportional to how many of those a corpus has rather than to its size.
        std::vector<std::size_t> shortNames;
        for (std::size_t i = 0u; i < count; ++i)
            if (tokenSet(mentions[i].name).size() == 1u && mentions[i].name.size() <= 8u)
                shortNames.push_back(i);
        for (std::size_t a : shortNames)
        {
            for (std::size_t j = 0u; j < count; ++j)
            {
                if (a == j || sets.find(a) == sets.find(j))
                    continue;
                if (seenPairs.count({a < j ? a : j, a < j ? j : a}) != 0u)
                    continue;
                if (!isAbbreviationOf(mentions[a].name, mentions[j].name))
                    continue;
                ++out.comparisons;
                out.candidates.push_back(Candidate{mentions[a < j ? a : j].id, mentions[a < j ? j : a].id,
                                                   params.proposeThreshold, Evidence::Abbreviation});
            }
        }
    }

    // ── Labels ────────────────────────────────────────────────────────────────
    // The representative is the smallest MENTION ID in the set, which is not the same as the
    // smallest index unless the caller numbered them in order. Resolved in two passes so the
    // answer does not depend on which member happened to be visited first.
    std::map<std::size_t, core::u32> smallest;
    for (std::size_t i = 0u; i < count; ++i)
    {
        const std::size_t root = sets.find(i);
        const auto found = smallest.find(root);
        if (found == smallest.end() || mentions[i].id < found->second)
            smallest[root] = mentions[i].id;
    }
    for (std::size_t i = 0u; i < count; ++i)
        out.representative[i] = smallest[sets.find(i)];
    out.clusters = static_cast<core::u32>(smallest.size());

    std::stable_sort(out.candidates.begin(), out.candidates.end(),
                     [](const Candidate &a, const Candidate &b) { return a.score > b.score; });
    return true;
}

bool countIndependentAgreements(const std::vector<core::u32> &sources, const std::vector<std::string> &references,
                                const std::vector<Testimony> &testimonies,
                                const std::vector<Derivation> &derivations, AgreementReport &out)
{
    out = AgreementReport{};
    if (sources.size() != references.size())
        return false;

    std::map<core::u32, std::size_t> indexOf;
    for (std::size_t i = 0u; i < sources.size(); ++i)
        if (!indexOf.emplace(sources[i], i).second)
            return false;

    out.agreements.assign(sources.size(), 0u);
    if (sources.empty())
        return true;

    DisjointSet witnesses{sources.size()};

    // Same address, one witness. The preprint and the published paper are one piece of work
    // however many rows a search provider returned for it.
    std::map<std::string, std::size_t> byReference;
    for (std::size_t i = 0u; i < sources.size(); ++i)
    {
        const std::string key = normaliseReference(references[i]);
        if (key.empty())
            continue; // unaddressed sources are never equal to one another
        const auto found = byReference.find(key);
        if (found == byReference.end())
            byReference[key] = i;
        else if (witnesses.unite(found->second, i))
            ++out.collapsed;
    }

    // Declared derivations. A source that copied another is not a second witness to what it
    // copied, and this is the only way that can be known — see @ref Derivation.
    for (const Derivation &edge : derivations)
    {
        const auto a = indexOf.find(edge.derived);
        const auto b = indexOf.find(edge.original);
        if (a == indexOf.end() || b == indexOf.end())
            continue; // an edge naming a source outside this batch constrains nothing here
        if (witnesses.unite(a->second, b->second))
            ++out.collapsed;
    }

    std::set<std::size_t> roots;
    for (std::size_t i = 0u; i < sources.size(); ++i)
        roots.insert(witnesses.find(i));
    out.witnesses = static_cast<core::u32>(roots.size());

    // ── Per claim: how many DISTINCT witnesses, and how many rows pretended to be ──
    std::map<core::u32, std::set<std::size_t>> witnessesPerClaim;
    std::map<core::u32, std::set<std::size_t>> rowsPerClaim;
    for (const Testimony &testimony : testimonies)
    {
        const auto found = indexOf.find(testimony.source);
        if (found == indexOf.end())
            continue;
        witnessesPerClaim[testimony.claim].insert(witnesses.find(found->second));
        rowsPerClaim[testimony.claim].insert(found->second);
    }

    for (const auto &entry : witnessesPerClaim)
    {
        const std::size_t independent = entry.second.size();
        if (independent > 1u)
            ++out.corroborated;
        // The number this whole file exists to produce: several sources assert it, one
        // witness attests it. Silently fusing those would be `fuseConfidence` manufacturing
        // certainty out of a single document cited twice.
        if (independent == 1u && rowsPerClaim[entry.first].size() > 1u)
            ++out.inflated;

        for (const Testimony &testimony : testimonies)
        {
            if (testimony.claim != entry.first)
                continue;
            const auto found = indexOf.find(testimony.source);
            if (found == indexOf.end())
                continue;
            // OTHER witnesses: a source does not corroborate itself, and neither does
            // anything that turned out to be it.
            const core::u32 others = static_cast<core::u32>(independent - 1u);
            if (others > out.agreements[found->second])
                out.agreements[found->second] = others;
        }
    }

    return true;
}

} // namespace lpl::harvest
