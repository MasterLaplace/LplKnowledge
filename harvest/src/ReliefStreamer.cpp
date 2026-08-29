/**
 * @file ReliefStreamer.cpp
 * @brief Mapping the tiles a plan asks for, and releasing the ones it does not.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/ReliefStreamer.hpp>

#include <lpl/knowledge/ReliefSource.hpp>

namespace lpl::harvest {

void ReliefStreamer::configure(std::string base, const math::ReliefResidencyParams &params)
{
    _base = std::move(base);
    _params = params;
    _slots.clear();
    _mosaic = math::ReliefMosaic{};
}

core::u64 ReliefStreamer::mappedBytes() const noexcept
{
    core::u64 total = 0u;
    for (const auto &slot : _slots)
        total += slot->file.size();
    return total;
}

ReliefStreamReport ReliefStreamer::update(core::i32 eyeCellX, core::i32 eyeCellZ)
{
    ReliefStreamReport report{};

    math::ReliefTileRequest wanted[math::kMaxResidentReliefTiles];
    const core::u32 count =
        math::planReliefResidency(_params, eyeCellX, eyeCellZ, wanted, math::kMaxResidentReliefTiles);

    for (auto &slot : _slots)
        slot->wanted = false;

    // Mark what is already held. Only what is newly wanted is mapped and only what stopped being
    // wanted is released: a streamer that rebuilt every frame would remap unchanged tiles, and
    // standing still would cost what travelling costs.
    for (core::u32 i = 0u; i < count; ++i)
    {
        for (auto &slot : _slots)
        {
            if (slot->key.level == wanted[i].level && slot->key.tileX == wanted[i].tileX &&
                slot->key.tileZ == wanted[i].tileZ)
            {
                slot->wanted = true;
                break;
            }
        }
    }

    // Release first, so a plan that swaps one tile for another never needs room for both.
    for (auto it = _slots.begin(); it != _slots.end();)
    {
        if ((*it)->wanted)
        {
            ++it;
            continue;
        }
        ++report.evicted;
        it = _slots.erase(it);
    }

    for (core::u32 i = 0u; i < count; ++i)
    {
        bool held = false;
        for (const auto &slot : _slots)
        {
            if (slot->key.level == wanted[i].level && slot->key.tileX == wanted[i].tileX &&
                slot->key.tileZ == wanted[i].tileZ)
            {
                held = true;
                break;
            }
        }
        if (held)
            continue;

        auto slot = std::make_unique<Slot>();
        slot->key = wanted[i];
        const std::string path = reliefTilePath(_base, wanted[i].level, wanted[i].tileX, wanted[i].tileZ);

        // A missing tile is NORMAL at the edge of a survey, and is counted separately from one that
        // is present and unreadable. Collapsing the two would make "this survey stops here" and
        // "this image is corrupt" the same line in a log.
        if (!slot->file.open(path))
        {
            ++report.missing;
            continue;
        }
        if (slot->pack.open(slot->file.bytes(), slot->file.size()) != knowledge::OpenStatus::Ok ||
            !knowledge::makeReliefField(slot->pack, slot->field))
        {
            ++report.rejected;
            continue;
        }

        // The level travels with the tile so the mosaic can prefer the finest. Taken from the plan
        // rather than from the image, because it is the plan that decided which level this tile is
        // standing in for.
        slot->field.level = wanted[i].level;
        _slots.push_back(std::move(slot));
        ++report.loaded;
    }

    // Rebuilt from what is actually held, every update. The mosaic is a view, not a record: a
    // stale entry in it is a pointer into an unmapped page, which reads as ground until it does not.
    _mosaic = math::ReliefMosaic{};
    for (const auto &slot : _slots)
        (void) _mosaic.add(&slot->field);

    report.resident = static_cast<core::u32>(_slots.size());
    return report;
}

} // namespace lpl::harvest
