/**
 * @file ReliefSource.cpp
 * @brief Pointing the generator at a baked survey.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/knowledge/ReliefSource.hpp>

namespace lpl::knowledge {

bool makeReliefField(const KnowledgePack &pack, math::ReliefField &out, core::u32 blendCells) noexcept
{
    out = math::ReliefField{};

    ReliefV1 header{};
    if (!pack.relief(header))
        return false;
    const core::i16 *samples = pack.reliefSamples();
    if (samples == nullptr || header.width == 0u || header.height == 0u)
        return false;

    math::GeoProjection spec{};
    // Read from the image, never rebuilt: a second projection is the same world in another valley.
    spec.originLatitudeRaw = header.latitudeRaw;
    spec.originLongitudeRaw = header.longitudeRaw;
    spec.referenceLatitude = header.referenceLatitude;
    spec.metresPerCell = header.metresPerCell;
    spec.unitsPerMetre = math::Fixed32::fromRaw(header.unitsPerMetreRaw);
    spec.seaLevelUnits = math::Fixed32::fromRaw(header.seaLevelUnitsRaw);

    out.samples = samples;
    out.width = header.width;
    out.height = header.height;
    out.originCellX = header.originCellX;
    out.originCellZ = header.originCellZ;
    out.projection = math::makeReliefProjection(spec);
    out.blendCells = blendCells == kUseBakedBlend ? header.blendCells : blendCells;
    out.exposedEdges = header.exposedEdges;
    // The detail layer stays silent: roughness is the world's decision, not the survey's, and a
    // field that arrived carrying invented detail would be measured ground quietly overwritten.
    return true;
}

} // namespace lpl::knowledge
