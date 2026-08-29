/**
 * @file PlaceResolver.cpp
 * @brief Answering where a place is from a baked corpus.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/knowledge/PlaceResolver.hpp>

namespace lpl::knowledge {

core::u32 PackPlaceResolver::placeCount() const noexcept
{
    return _pack == nullptr ? 0u : _pack->gazetteerCount();
}

bool PackPlaceResolver::resolve(core::u32 id, history::Place &out) const
{
    out = history::Place{};
    if (_pack == nullptr || id == kNoIdentifier)
        return false;

    GazetteerEntryV1 wire{};
    if (!_pack->placeById(id, wire))
        return false;

    out.id = wire.place;
    // @warning The raw words, verbatim. The whole reason a coordinate travels as a Q16.16 word is
    // that the bits which come back are the bits that went in: converting through a float here
    // would put a rounding between the corpus and the walk, and two targets could then disagree
    // about where a body ends up.
    out.x = math::Fixed32::fromRaw(wire.lonRaw);
    out.z = math::Fixed32::fromRaw(wire.latRaw);
    out.located = (wire.flags & kGazetteerFlagLocated) != 0u;

    // @warning An UNDATED place answers true for every year, which `existsInYear` already encodes --
    // so the window is left at zero rather than being filled with the era's bounds. Absence of a
    // window is absence of knowledge, not a claim that the place never existed, and 2815 of the
    // corpus's places have no dating anybody has settled.
    if ((wire.flags & kGazetteerFlagDated) != 0u)
    {
        out.minYear = wire.minYear;
        out.maxYear = wire.maxYear;
    }
    return true;
}

core::u32 PackPlaceResolver::linkedPlaces(core::u32 id, core::u32 *out, core::u32 capacity) const
{
    if (_pack == nullptr || out == nullptr || capacity == 0u || id == kNoIdentifier)
        return 0u;

    core::u32 written = 0u;
    const core::u32 links = _pack->placeLinkCount();
    for (core::u32 i = 0u; i < links && written < capacity; ++i)
    {
        PlaceLinkV1 link{};
        if (!_pack->placeLinkAt(i, link) || link.from != id)
            continue;
        // @warning A link to a place this corpus does not carry is skipped rather than returned. The
        // walk would ask the resolver for it a moment later, get nothing, and treat a stated road
        // as a dead end -- which reads as the corpus being silent when it was actually specific.
        GazetteerEntryV1 exists{};
        if (!_pack->placeById(link.to, exists))
            continue;
        out[written++] = link.to;
    }
    return written;
}

} // namespace lpl::knowledge
