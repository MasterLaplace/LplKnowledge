/**
 * @file History.cpp
 * @brief Implementation of the bridge between an image and the arithmetic of doubt.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/knowledge/History.hpp>

#if defined(LPL_HAS_FOUNDATION)

namespace lpl::knowledge {

namespace {

/// Raw Q16.16 word for one. A confidence above this is corrupt, not emphatic.
constexpr core::u32 kOneRaw = 65536u;

} // namespace

bool fromWireFact(const FactV1 &wire, history::Fact &out) noexcept
{
    out = history::Fact{};

    if (wire.toYear < wire.fromYear)
        return false;
    if (wire.confidenceRaw > kOneRaw)
        return false;

    out.subject = wire.subject;
    out.predicate = wire.predicate;
    out.object = wire.object;
    out.fromYear = wire.fromYear;
    out.toYear = wire.toYear;
    out.source = wire.source;
    out.sigma = math::Fixed32::fromRaw(static_cast<core::i32>(wire.confidenceRaw));
    return true;
}

void toWireFact(const history::Fact &fact, core::u32 locus, FactV1 &out) noexcept
{
    out = FactV1{};
    out.subject = fact.subject;
    out.predicate = fact.predicate;
    out.object = fact.object;
    out.fromYear = fact.fromYear;
    out.toYear = fact.toYear;
    out.source = fact.source;
    // The raw word verbatim, never a float round trip: the point of carrying Q16.16 on the
    // wire is that the bits that come back are the bits that went in.
    out.confidenceRaw = static_cast<core::u32>(fact.sigma.raw());
    out.locus = locus;
}

bool fromWireSource(const SourceV1 &wire, history::SourceProfile &out) noexcept
{
    out = history::SourceProfile{};

    if (wire.kind >= static_cast<core::u32>(SourceKindV1::Count))
        return false;

    out.id = wire.id;
    out.kind = static_cast<history::SourceKind>(wire.kind);
    out.yearsAfterEvent = wire.yearsAfterEvent;
    out.independentAgreements = wire.agreements;
    return true;
}

void toWireSource(const history::SourceProfile &profile, core::u32 name, core::u32 document,
                  SourceV1 &out) noexcept
{
    out = SourceV1{};
    out.id = profile.id;
    out.kind = static_cast<core::u32>(profile.kind);
    out.yearsAfterEvent = profile.yearsAfterEvent;
    out.agreements = profile.independentAgreements;
    out.name = name;
    out.document = document;
}

bool toHistoryCorpus(const KnowledgePack &pack, history::Corpus &out, DecodeReport &report)
{
    out.facts.clear();
    out.sources.clear();
    report = DecodeReport{};

    if (!pack.ready())
        return false;

    // Sources first, so a caller that inspects a partial result finds the profiles a fact
    // refers to already present rather than half of each.
    for (core::u32 i = 0u; i < pack.sourceCount(); ++i)
    {
        SourceV1 wire{};
        if (!pack.sourceAt(i, wire))
            continue;

        history::SourceProfile profile;
        if (!fromWireSource(wire, profile))
        {
            ++report.badKind;
            continue;
        }
        out.sources.push_back(profile);
        ++report.sources;
    }

    for (core::u32 i = 0u; i < pack.factCount(); ++i)
    {
        FactV1 wire{};
        if (!pack.factAt(i, wire))
            continue;

        history::Fact fact;
        if (!fromWireFact(wire, fact))
        {
            // Told apart rather than counted together: a reversed window is a baker that
            // wrote the fields in the wrong order, a confidence above one is a damaged
            // byte. The two send a reader looking in different places.
            if (wire.toYear < wire.fromYear)
                ++report.badWindow;
            else
                ++report.badConfidence;
            continue;
        }
        out.facts.push_back(fact);
        ++report.facts;
    }

    return report.badKind == 0u && report.badWindow == 0u && report.badConfidence == 0u;
}

bool corporaMatch(const history::Corpus &a, const history::Corpus &b) noexcept
{
    if (a.facts.size() != b.facts.size() || a.sources.size() != b.sources.size())
        return false;

    for (core::usize i = 0u; i < a.facts.size(); ++i)
    {
        const history::Fact &x = a.facts[i];
        const history::Fact &y = b.facts[i];
        if (x.subject != y.subject || x.predicate != y.predicate || x.object != y.object)
            return false;
        if (x.fromYear != y.fromYear || x.toYear != y.toYear || x.source != y.source)
            return false;
        // Compared on the RAW word, not with operator==: two Fixed32 that differ by one
        // unit in the last place are a round-trip that lost a bit, and any comparison with
        // a tolerance would be a comparison that cannot see it.
        if (x.sigma.raw() != y.sigma.raw())
            return false;
    }

    for (core::usize i = 0u; i < a.sources.size(); ++i)
    {
        const history::SourceProfile &x = a.sources[i];
        const history::SourceProfile &y = b.sources[i];
        if (x.id != y.id || x.kind != y.kind)
            return false;
        if (x.yearsAfterEvent != y.yearsAfterEvent || x.independentAgreements != y.independentAgreements)
            return false;
    }

    return true;
}

} // namespace lpl::knowledge

#endif // LPL_HAS_FOUNDATION
