/**
 * @file AuthorDates.cpp
 * @brief Joining an author's dates across catalogues that share no identifier.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/AuthorDates.hpp>

#include <lpl/harvest/EntityResolution.hpp>

#include <algorithm>

namespace lpl::harvest {

namespace {

const AuthorWindow kNoWindow{};

/**
 * @brief The form two catalogues can be compared in.
 *
 * @warning Case and punctuation only. A catalogue writes "Herodotus", another "Herodotus." and a
 * third "HERODOTUS"; those are one spelling of one name, and refusing to see it would lose the
 * join for a reason no reader cares about. What is NOT folded is anything that changes which
 * words are there -- "Martin, Jean" and "Martin, Jean-Baptiste" stay different, because they may
 * be different men.
 *
 * @param name The name as spelled.
 * @return The comparable form.
 */
[[nodiscard]] std::string normalisedName(std::string_view name)
{
    std::string out;
    out.reserve(name.size());
    bool space = false;
    for (const char c : name)
    {
        char lower = c;
        if (lower >= 'A' && lower <= 'Z')
            lower = static_cast<char>(lower - 'A' + 'a');
        const bool word = (lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9');
        if (word)
        {
            if (space && !out.empty())
                out.push_back(' ');
            space = false;
            out.push_back(lower);
            continue;
        }
        space = true;
    }
    return out;
}

} // namespace

void AuthorDates::add(std::string_view name, core::i32 fromDay, core::i32 toDay)
{
    Entry entry;
    entry.window.name.assign(name);
    entry.window.fromDay = fromDay;
    entry.window.toDay = toDay;
    entry.normalised = normalisedName(name);
    _entries.push_back(std::move(entry));
    _finalised = false;
}

void AuthorDates::finalise()
{
    // Stable, so entries that normalise alike keep the order the caller added them: two runs over
    // one catalogue must resolve to the same entry, and "whichever the sort happened to move
    // first" is a fact about the algorithm rather than about the corpus.
    std::stable_sort(_entries.begin(), _entries.end(),
                     [](const Entry &a, const Entry &b) { return a.normalised < b.normalised; });
    _finalised = true;
}

const AuthorWindow &AuthorDates::at(core::u32 index) const
{
    if (index >= _entries.size())
        return kNoWindow;
    return _entries[index].window;
}

AuthorDateMatch AuthorDates::find(std::string_view name, core::u32 proposeThreshold) const
{
    AuthorDateMatch out;
    if (!_finalised || _entries.empty() || name.empty())
        return out;

    const std::string wanted = normalisedName(name);
    if (wanted.empty())
        return out;

    // Exact first, by bisection over the normalised form.
    const auto lower = std::lower_bound(_entries.begin(), _entries.end(), wanted,
                                        [](const Entry &e, const std::string &v) { return e.normalised < v; });
    if (lower != _entries.end() && lower->normalised == wanted)
    {
        out.index = static_cast<core::u32>(lower - _entries.begin());
        out.exact = true;
        out.scoreRaw = 65536u;
        out.fromDay = lower->window.fromDay;
        out.toDay = lower->window.toDay;
        return out;
    }

    // @warning Nothing is filled from here down. A resemblance is a reason to look, and turning it
    // into a date would be exactly the step that makes a corpus confident and wrong: two men named
    // Jean Martin, a notary at Rouen and one at Rennes, resemble each other completely.
    core::u32 best = 0u;
    core::u32 bestIndex = 0u;
    for (core::usize i = 0u; i < _entries.size(); ++i)
    {
        const core::u32 score = nameSimilarity(wanted, _entries[i].normalised);
        // Ties go to the LOWER index, which after a stable sort is the entry a caller added
        // first -- a rule, rather than whichever the scan reached last.
        if (score > best)
        {
            best = score;
            bestIndex = static_cast<core::u32>(i);
        }
    }
    if (best >= proposeThreshold && proposeThreshold != 0u)
    {
        out.proposed = true;
        out.scoreRaw = best;
        out.index = bestIndex;
    }
    return out;
}

} // namespace lpl::harvest
