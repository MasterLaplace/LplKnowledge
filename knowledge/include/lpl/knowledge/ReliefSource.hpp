/**
 * @file ReliefSource.hpp
 * @brief Handing a baked survey to the generator, without copying a byte of it.
 *
 * @warning **The dependency runs one way, and this file is which way.** A survey is a corpus, so it
 * lives here; the generator that reads ground lives in LplPlugin, which knows nothing about images.
 * The same arrangement as @ref PlaceResolver.hpp: this side adapts, the other side declares. The
 * generator's `math::ReliefField` is deliberately a plain non-owning view precisely so an image
 * can BE one rather than be copied into one.
 *
 * @warning Freestanding-safe: no heap, no filesystem, no allocation at all. The field it builds
 * points straight into the mapped image, which in ring 0 is a section of a boot module that was
 * never copied anywhere.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_KNOWLEDGE_RELIEFSOURCE_HPP
#    define LPL_LPL_KNOWLEDGE_RELIEFSOURCE_HPP

#    include <lpl/knowledge/KnowledgePack.hpp>

#    include <lpl/math/Geo.hpp>

namespace lpl::knowledge {

/**
 * @warning The two constants must be the same word, because a gap crosses this boundary unchanged.
 * If they ever parted, a cell nobody measured would arrive on the generator's side as ground
 * thirty-two kilometres below the sea -- a plausible-looking number, in a place a reader would have
 * to visit to doubt.
 */
static_assert(kReliefNoSampleWire == math::kReliefNoSample,
              "the wire's gap marker and the generator's must be one value");

/**
 * @brief Builds a generator-side view of an image's measured ground.
 *
 * @warning **Nothing is copied and nothing is resampled.** The samples were put into cell space by
 * the baker, using this same projection, so the reading side has only to point at them. A reader
 * that resampled would be the second resampler, and the two would answer differently at exactly
 * the cells where it matters.
 *
 * @warning The projection is READ from the image rather than rebuilt from the recipe. Rebuilding it
 * would be a second projection, and a world whose samples were laid down under one and read under
 * another is the same world in two different valleys.
 *
 * @param pack       An opened image.
 * @param blendCells Cells over which real ground gives way to invented ground, or `kUseBakedBlend`
 *                   to take what the image declares. An override exists because how far a world may
 *                   be walked past its survey is a decision about the world, not about the survey.
 * @param out        Receives the field; left empty when the image carries no relief.
 * @return false when the image carries no relief, which is not an error: most images are corpora
 *         with nothing to stand on.
 */
[[nodiscard]] bool makeReliefField(const KnowledgePack &pack, math::ReliefField &out,
                                   core::u32 blendCells = 0xFFFFFFFFu) noexcept;

/// Passed as `blendCells` to keep whatever the image was baked with.
inline constexpr core::u32 kUseBakedBlend = 0xFFFFFFFFu;

} // namespace lpl::knowledge

#endif // LPL_LPL_KNOWLEDGE_RELIEFSOURCE_HPP
