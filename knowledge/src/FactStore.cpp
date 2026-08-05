/**
 * @file FactStore.cpp
 * @brief Implementation of bounded, non-owning access to the facts in a pack.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/knowledge/FactStore.hpp>

namespace lpl::knowledge {

void FactStore::run(const Query &query, Page &out) const noexcept
{
    out = Page{};
    if (_pack == nullptr || !_pack->ready())
        return;

    // The cap is the smaller of what the caller asked for and what a page holds. Taking
    // the caller's number on trust would overrun the array; ignoring it would return more
    // than was asked for, and both are ways of not answering the question.
    const core::u32 cap = query.limit < kMaxPageRows ? query.limit : kMaxPageRows;

    const core::u32 total = _pack->factCount();
    for (core::u32 i = 0u; i < total; ++i)
    {
        FactV1 fact{};
        if (!_pack->factAt(i, fact))
            continue;
        if (!matches(query, fact))
            continue;

        ++out.matched;
        if (out.count < cap)
            out.rows[out.count++] = fact;
    }

    out.truncated = out.matched > out.count ? 1u : 0u;
}

core::u32 FactStore::count(const Query &query) const noexcept
{
    if (_pack == nullptr || !_pack->ready())
        return 0u;

    core::u32 matched = 0u;
    const core::u32 total = _pack->factCount();
    for (core::u32 i = 0u; i < total; ++i)
    {
        FactV1 fact{};
        if (_pack->factAt(i, fact) && matches(query, fact))
            ++matched;
    }
    return matched;
}

bool FactStore::best(const Query &query, FactV1 &out) const noexcept
{
    if (_pack == nullptr || !_pack->ready())
        return false;

    bool found = false;
    const core::u32 total = _pack->factCount();
    for (core::u32 i = 0u; i < total; ++i)
    {
        FactV1 fact{};
        if (!_pack->factAt(i, fact) || !matches(query, fact))
            continue;

        // Strictly greater, so the first of two equally held claims wins. Written this
        // way rather than with `>=` on purpose: with `>=` the answer would be the LAST
        // tied row, which makes it depend on the baked order in a direction nobody
        // chose.
        if (!found || fact.confidenceRaw > out.confidenceRaw)
        {
            out = fact;
            found = true;
        }
    }
    return found;
}

core::u32 FactStore::foldPage(const Page &page) noexcept
{
    core::u32 hash = kFnv1aOffsetBasis;
    for (core::u32 i = 0u; i < page.count; ++i)
    {
        const FactV1 &row = page.rows[i];
        foldWord(hash, row.subject);
        foldWord(hash, row.predicate);
        foldWord(hash, row.object);
        foldWord(hash, static_cast<core::u32>(row.fromYear));
        foldWord(hash, static_cast<core::u32>(row.toYear));
        foldWord(hash, row.source);
        foldWord(hash, row.confidenceRaw);
        foldWord(hash, row.locus);
    }
    foldWord(hash, page.count);
    foldWord(hash, page.matched);
    foldWord(hash, page.truncated);
    return hash;
}

} // namespace lpl::knowledge
