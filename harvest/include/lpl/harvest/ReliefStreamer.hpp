/**
 * @file ReliefStreamer.hpp
 * @brief Executing a residency plan: the half of streaming that touches a filesystem.
 *
 * @warning **This is what stops `math::planReliefResidency` being an orphan.** The plan was written
 * with no consumer, which by this repository's own rule is a feature that cannot be wrong because
 * nothing runs it. Choosing which tiles to hold is arithmetic and lives in `math`, callable from
 * ring 0; LOADING them needs a filesystem, so it lives here, on the side that has one.
 *
 * @warning **A slot owns its mapping AND its field, and that pairing is the invariant.**
 * `math::ReliefField::samples` points straight into the mapped image -- that is the whole reason a
 * survey costs nothing to hold -- so a mapping released while its field is still in the mosaic
 * leaves the world reading unmapped memory. Nothing about a dangling field looks wrong until the
 * ground under a body is whatever the kernel put there next.
 *
 * @warning **A tile that is not resident is not a hole.** Levels overlap by design, so ground the fine
 * tiles no longer cover is still covered by the coarse ones underneath, and
 * `math::ReliefMosaic::find` prefers the finest of whatever is there. That is why eviction does not
 * need to fade anything: the baked `exposedEdges` describes the edge of the SURVEY, which does not
 * move, while residency changes every frame. Recomputing the fade from what happens to be loaded
 * would make tiles pulse as they stream, and every pulse would look like terrain.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_RELIEFSTREAMER_HPP
#    define LPL_LPL_HARVEST_RELIEFSTREAMER_HPP

#    include <lpl/harvest/MappedFile.hpp>
#    include <lpl/harvest/Relief.hpp>
#    include <lpl/knowledge/KnowledgePack.hpp>

#    include <lpl/math/Geo.hpp>

#    include <memory>
#    include <string>
#    include <vector>

namespace lpl::harvest {

/**
 * @struct ReliefStreamReport
 * @brief What the last update did.
 */
struct ReliefStreamReport {
    core::u32 resident{0u}; ///< Tiles held after the update.
    core::u32 loaded{0u};   ///< Mapped this update.
    core::u32 evicted{0u};  ///< Released this update.
    core::u32 missing{0u};  ///< Wanted, but no such file. Normal at the edge of a survey.
    core::u32 rejected{0u}; ///< Present but unreadable, which is NOT normal.
};

/**
 * @class ReliefStreamer
 * @brief Keeps the tiles a plan asks for mapped, and the rest not.
 */
class ReliefStreamer {
public:
    /**
     * @brief Points the streamer at a baked survey.
     *
     * @warning **`params.tileCells` MUST be the tile size the survey was baked with**, and getting it
     * wrong fails quietly rather than loudly: the plan asks for tile indices in a lattice the bake
     * never used, so most requests name files that are not there and the walk finds no ground under
     * it -- measured, a plan of 1024 against a bake of 32 answered 64 cells out of 200 and called
     * the rest missing, which is indistinguishable from walking off the edge of the survey. The
     * size is not discoverable before the first tile is opened, and opening one to find out how to
     * decide which one to open is a circle; so it is the caller's to keep true, and
     * `ReliefBakeReport` prints what a bake used.
     *
     * @param base   Path without extension, as @ref reliefTilePath names its parts.
     * @param params How much ground to hold, and at what detail.
     */
    void configure(std::string base, const math::ReliefResidencyParams &params);

    /**
     * @brief Brings residency in line with where the eye is.
     *
     * @warning Loads only what is newly wanted and releases only what is no longer wanted, rather
     * than rebuilding: a streamer that dropped everything and reloaded would map the same
     * unchanged tiles every frame, and the cost of standing still would be the cost of travelling.
     *
     * @param eyeCellX Where the eye is, in level-0 cells.
     * @param eyeCellZ Where the eye is.
     * @return What it did.
     */
    ReliefStreamReport update(core::i32 eyeCellX, core::i32 eyeCellZ);

    /**
     * @brief The ground, as the generator reads it.
     *
     * @warning Rebuilt on every @ref update and invalidated by it: the mosaic holds pointers to
     * fields whose slots may have been released. Read it after updating, never across one.
     *
     * @return The resident tiles.
     */
    [[nodiscard]] const math::ReliefMosaic &mosaic() const noexcept { return _mosaic; }

    /// @return How many tiles are mapped right now.
    [[nodiscard]] core::u32 residentCount() const noexcept { return static_cast<core::u32>(_slots.size()); }

    /// @return Bytes of image currently mapped.
    [[nodiscard]] core::u64 mappedBytes() const noexcept;

private:
    /**
     * @struct Slot
     * @brief One mapped tile: the file, the reader over it, and the field into it.
     *
     * @warning The three cannot be separated. Held by pointer so that growing the slot list never
     * moves a field a mosaic is already pointing at.
     */
    struct Slot {
        math::ReliefTileRequest key{};
        MappedFile file;
        knowledge::KnowledgePack pack;
        math::ReliefField field;
        bool wanted{false};
    };

    std::string _base;
    math::ReliefResidencyParams _params{};
    std::vector<std::unique_ptr<Slot>> _slots;
    math::ReliefMosaic _mosaic{};
};

} // namespace lpl::harvest

#endif // LPL_LPL_HARVEST_RELIEFSTREAMER_HPP
