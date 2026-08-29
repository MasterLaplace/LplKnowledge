/**
 * @file Relief.hpp
 * @brief Real ground, read from the tiles a satellite left behind.
 *
 * @warning **The format was chosen by measurement, and the first choice was wrong.** A raw global grid
 * at one arc-minute -- 1850 metres a cell -- was picked because it needed no parser, which is a
 * reason about the reader rather than about the world: nobody walks on a 1850-metre cell. Measured
 * against the alternatives, the BEST resolution and the SIMPLEST format turned out to be the same
 * file: one arc-second, about 31 metres, as a bare big-endian `i16` grid with no header at all.
 * The trade-off did not exist.
 *
 * Verified against the ground rather than assumed: the tile covering the southern Peloponnese
 * peaks at **2390 m** where Taygetus really rises to 2404, and the Messenian Gulf comes back at
 * **-819 m**. Rows run north to south, columns west to east, and the product already carries
 * bathymetry -- so a coastline falls out of the sign of the sample and needs no second dataset.
 *
 * @warning The 14 metres missing from that summit are SRTM smoothing ridges, not a misread. It is the
 * accuracy of the data, and it is stated here rather than discovered by somebody comparing a
 * rendered mountain against an atlas.
 *
 * @warning **Attribution travels IN the image.** The sources are public domain (USGS SRTM, GMTED2010,
 * NOAA ETOPO1) and redistribution is permitted -- conditionally on attribution. An image that
 * carries the samples and leaves the credit in a README that gets separated from it is an image
 * whose redistribution breaks the licence. See @ref reliefAttribution.
 *
 * @warning Host only: this reads files and allocates. The samples cross into ring 0 through a section,
 * byte-swapped ONCE here, so nothing on the far side ever sees a big-endian word.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_RELIEF_HPP
#    define LPL_LPL_HARVEST_RELIEF_HPP

#    include <lpl/Foundation.hpp>

#    include <lpl/math/Geo.hpp>

#    include <string>
#    include <string_view>
#    include <vector>

namespace lpl::harvest {

/**
 * Elevation a tile uses to mean "no measurement here".
 *
 * @warning It must never be averaged with real samples. SRTM voids are this exact value, and one of
 * them folded into a downsampled block drags a mountainside to the bottom of the sea -- a
 * plausible-looking number, in a place a reader would have to visit to doubt.
 */
inline constexpr core::i16 kReliefVoid = -32768;

/**
 * @brief The credit this data must carry wherever it goes.
 *
 * @warning Not decoration. Redistribution is permitted BECAUSE of attribution, so an image holding
 * these samples without it is one nobody may pass on. Returned as a string so a baker can put it
 * in the image's own Texts section rather than in a file beside it.
 *
 * @return The attribution line, verbatim as the sources require it.
 */
[[nodiscard]] std::string_view reliefAttribution() noexcept;

/**
 * @struct ReliefTile
 * @brief One degree of ground, as the file holds it.
 */
struct ReliefTile {
    core::i32 southLatitude{0}; ///< Degrees of the tile's SOUTH edge; negative is south.
    core::i32 westLongitude{0}; ///< Degrees of its WEST edge; negative is west.
    core::u32 side{0u};         ///< Samples per side; 3601 at one arc-second, 1201 at three.
    std::vector<core::i16> samples; ///< Row-major, NORTH row first, native byte order.

    /**
     * @brief Reads one sample.
     *
     * @param row Zero at the north edge.
     * @param col Zero at the west edge.
     * @return The elevation in metres, or @ref kReliefVoid.
     */
    [[nodiscard]] core::i16 at(core::u32 row, core::u32 col) const
    {
        return row < side && col < side ? samples[static_cast<core::usize>(row) * side + col] : kReliefVoid;
    }
};

/**
 * @struct ReliefReadReport
 * @brief What reading a tile found, and what it refused.
 */
struct ReliefReadReport {
    core::u32 voids{0u};      ///< Samples with no measurement.
    core::i16 lowest{0};      ///< Lowest real sample; bathymetry makes this negative at a coast.
    core::i16 highest{0};     ///< Highest real sample.
    bool malformed{false};    ///< The byte count is not a square of i16, or the side is unknown.
};

/**
 * @brief Reads a `.hgt` tile.
 *
 * @warning **The side is DEDUCED from the byte count, and an unrecognised one is refused.** The format
 * has no header at all -- that is what makes it free to parse and also what makes it impossible
 * to validate from the inside. A file of the wrong size read as a guessed side produces a
 * perfectly plausible grid of the wrong shape, which is the failure a reader must not have.
 *
 * @warning The name carries the corner: `N37E023.hgt` is the degree whose SOUTH-WEST corner is 37N
 * 23E. Parsed from the filename because the bytes do not say, and stated wrong it puts a mountain
 * range in the sea.
 *
 * @param path      The file.
 * @param out       Receives the tile.
 * @param outReport Receives the tally.
 * @return false when the file cannot be read, or its shape is not one this reader knows.
 */
[[nodiscard]] bool readHeightTile(const std::string &path, ReliefTile &out, ReliefReadReport &outReport);

/**
 * @brief Reads a tile's south-west corner out of its filename.
 *
 * @param name       The filename, with or without directories, e.g. `N37E023.hgt`.
 * @param outLatitude  Receives the south edge in degrees.
 * @param outLongitude Receives the west edge in degrees.
 * @return false when the name does not carry a corner.
 */
[[nodiscard]] bool tileCorner(std::string_view name, core::i32 &outLatitude, core::i32 &outLongitude) noexcept;

/**
 * @struct ReliefRegion
 * @brief A rectangle of ground at one resolution, assembled from tiles.
 */
struct ReliefRegion {
    core::i32 southLatitude{0}; ///< South edge, degrees.
    core::i32 westLongitude{0}; ///< West edge, degrees.
    core::u32 widthDegrees{0u};  ///< How many degrees across.
    core::u32 heightDegrees{0u}; ///< How many degrees tall.
    core::u32 samplesPerDegree{0u}; ///< 3600 at one arc-second; 120 at thirty.
    core::u32 width{0u};   ///< Columns.
    core::u32 height{0u};  ///< Rows, NORTH first.
    std::vector<core::i16> samples;

    /**
     * @brief Reads one sample.
     *
     * @param row Zero at the north edge.
     * @param col Zero at the west edge.
     * @return The elevation, or @ref kReliefVoid outside the region.
     */
    [[nodiscard]] core::i16 at(core::u32 row, core::u32 col) const
    {
        return row < height && col < width ? samples[static_cast<core::usize>(row) * width + col] : kReliefVoid;
    }
};

/**
 * @brief Reduces a tile by an integer factor, averaging real samples only.
 *
 * @warning **An integer mean, and voids excluded from it.** The average of a block is a sum and a
 * divide, both exact, so two machines reduce the same tile identically -- which matters because
 * this output is baked and then folded. Letting a void into the sum would pull a coastal block
 * down by tens of thousands of metres; a block that is ALL void stays void, because inventing a
 * height for ground nobody measured is the one thing worse than admitting the gap.
 *
 * @warning The factor must divide `side - 1` exactly. A tile is 3601 across because it carries BOTH
 * edges -- the last column of one degree is the first of the next -- so reducing by a factor that
 * does not divide the 3600 intervals leaves a seam where two tiles meet.
 *
 * @param tile   What to reduce.
 * @param factor How many samples per output sample, per axis.
 * @param out    Receives the reduced tile, same corner, smaller side.
 * @return false when the factor does not divide the tile's intervals.
 */
[[nodiscard]] bool reduceTile(const ReliefTile &tile, core::u32 factor, ReliefTile &out);

/**
 * @brief Assembles tiles into one region, at a stated resolution.
 *
 * @warning Tiles SHARE their edges, so the seam column of one is the first column of the next. Written
 * once rather than twice: a region assembled by concatenation is one column wider per tile and
 * every place east of the first seam sits a fraction of a degree off.
 *
 * @param tiles            The tiles, in any order; those outside the rectangle are ignored.
 * @param southLatitude    South edge of the wanted rectangle, degrees.
 * @param westLongitude    West edge, degrees.
 * @param widthDegrees     How many degrees across.
 * @param heightDegrees    How many degrees tall.
 * @param samplesPerDegree Resolution of the output.
 * @param out              Receives the region.
 * @return false when a tile the rectangle needs is missing, which is refused rather than filled:
 *         a hole in a heightfield is a place a body falls through.
 */
[[nodiscard]] bool assembleRegion(const std::vector<ReliefTile> &tiles, core::i32 southLatitude,
                                  core::i32 westLongitude, core::u32 widthDegrees, core::u32 heightDegrees,
                                  core::u32 samplesPerDegree, ReliefRegion &out);

/**
 * @struct CellGrid
 * @brief A region resampled onto the world's cells, ready to be baked as-is.
 */
struct CellGrid {
    core::u32 width{0u};  ///< Columns.
    core::u32 height{0u}; ///< Rows, NORTH first.
    core::i32 originCellX{0}; ///< World cell of column zero.
    core::i32 originCellZ{0}; ///< World cell of row zero.
    std::vector<core::i16> samples; ///< Metres; @ref kReliefVoid where nobody measured.
    core::u32 gaps{0u};   ///< Cells with no measurement behind them.
    core::i16 lowest{0};  ///< Lowest real sample.
    core::i16 highest{0}; ///< Highest real sample.
};

/**
 * @brief Resamples a region onto world cells.
 *
 * @warning **This is the ONLY resampler, and it uses the projection the reading side will use.** An
 * arc-second never lines up with a thirty-metre cell, so something must resample; doing it here and
 * baking the result means ring 0 has only to index. A reader that resampled would be the second
 * one, and two resamplers disagree exactly at the cells where the ground changes fastest.
 *
 * @warning **Nearest neighbour, not bilinear, and that is a decision about voids rather than about
 * quality.** Interpolating across a gap mixes a real height with a marker that means "no
 * measurement", and the result is a smooth, plausible slope into ground nobody surveyed. Nearest
 * neighbour either lands on a measurement or lands on a gap, and says which.
 *
 * @param region     Assembled tiles, at their own resolution.
 * @param projection Where the cells are. The one the image will carry.
 * @param out        Receives the grid.
 * @return false when the region is empty or the rectangle does not overlap the projection at all.
 */
[[nodiscard]] bool resampleToCells(const ReliefRegion &region, const math::ReliefProjection &projection,
                                   CellGrid &out);

/**
 * @struct ReliefTilePlan
 * @brief How a survey is cut into the images a world streams in.
 *
 * @warning **Tiling is not an optimisation here, it is the only way the earth fits.** At thirty metres
 * a global survey is 1 335 833 by 666 800 cells -- 1.78 TB -- while the window a body can see is
 * under a megabyte. One image per tile rather than one image with many: a tile is the unit of
 * residency, so it should be the unit of loading, and mapping a multi-gigabyte file to read half a
 * megabyte of it wastes address space and page cache. It also puts the format's 4 GiB offset ceiling
 * out of reach entirely, instead of within 7% of it as the catalogue already came.
 */
struct ReliefTilePlan {
    core::u32 tileCells{1024u}; ///< Cells per tile side. A 1024 tile at 30 m is 31 km of ground, 2 MB.
    core::i32 firstTileX{0};    ///< Index of the westernmost tile.
    core::i32 firstTileZ{0};    ///< Index of the northernmost tile.
    core::u32 tilesX{1u};       ///< Tiles across.
    core::u32 tilesZ{1u};       ///< Tiles down.

    /**
     * @brief Which of a tile's edges face nothing, and therefore fade.
     *
     * @warning The fade belongs to the border of the SURVEY, never to the border of a tile: a tile
     * that faded on every side would ring itself in half-invented ground, and the world would come
     * out cross-hatched with a gentle valley at every boundary. Only the outside of the plan is
     * exposed.
     *
     * @param tileX Tile column.
     * @param tileZ Tile row.
     * @return A mask of `math::kReliefEdge*` bits.
     */
    [[nodiscard]] core::u32 exposedEdges(core::i32 tileX, core::i32 tileZ) const noexcept;
};

/**
 * @brief The file one tile of a survey lives in.
 *
 * @warning **Declared once and called by both ends**, the same rule `cataloguePartPath` states: a
 * writer and a reader that agree on a naming convention by coincidence are a bake nobody can find.
 *
 * @param base  Path without extension, e.g. `/data/earth`.
 * @param level Reduction level; zero is full resolution, each step is coarser ground.
 * @param tileX Tile column.
 * @param tileZ Tile row.
 * @return The path.
 */
[[nodiscard]] std::string reliefTilePath(const std::string &base, core::u32 level, core::i32 tileX,
                                         core::i32 tileZ);

/**
 * @struct ReliefBakeReport
 * @brief What a tiled bake wrote, and what it skipped.
 */
struct ReliefBakeReport {
    core::u32 tilesWritten{0u}; ///< Images produced.
    core::u32 tilesEmpty{0u};   ///< Tiles the survey had nothing for; skipped, not written empty.
    core::u64 bytesWritten{0u}; ///< Total.
    core::u32 cells{0u};        ///< Cells carried.
    core::u32 gaps{0u};         ///< Cells nobody measured.
};

/**
 * @brief Bakes a survey as one image per tile.
 *
 * @warning **Peak memory is ONE tile, whatever the size of the world.** That is the whole point, and
 * it is the same shape `CatalogueStream` took when HathiTrust's holdings needed a projected 11.9 GB
 * of RSS and came out at 7 MB. A tile is resampled, written and discarded before the next begins;
 * nothing accumulates. A relief bake is in fact easier than a catalogue's -- no sort, no dependency
 * between rows, and the size known before the first byte -- so it needs none of the three temporary
 * files the catalogue had to spill to.
 *
 * @warning An empty tile is SKIPPED rather than written empty, so "the survey has nothing here" and
 * "the tile is all sea" stay different statements. A reader that found an empty image could not
 * tell them apart.
 *
 * @param base       Path without extension; @ref reliefTilePath names the parts.
 * @param region     The assembled source, at its own resolution.
 * @param projection Where the cells are. Stored in every tile, never rebuilt.
 * @param plan       How to cut it.
 * @param blendCells Cells of fade at the survey's outer border.
 * @param credit     Attribution, carried into every tile. Redistribution is conditional on it.
 * @param outReport  Receives the tally.
 * @return false when a tile could not be written.
 */
[[nodiscard]] bool bakeReliefTiles(const std::string &base, const ReliefRegion &region,
                                   const math::ReliefProjection &projection, const ReliefTilePlan &plan,
                                   core::u32 blendCells, std::string_view credit, ReliefBakeReport &outReport);

} // namespace lpl::harvest

#endif // LPL_LPL_HARVEST_RELIEF_HPP
