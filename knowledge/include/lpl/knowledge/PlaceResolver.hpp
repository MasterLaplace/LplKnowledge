/**
 * @file PlaceResolver.hpp
 * @brief Where a place is, answered from a baked corpus rather than from a table in a binary.
 *
 * @warning **The seam this closes was named, not smuggled.** `history::IPlaceResolver` exists so a
 * walking body can ask where a place is without the simulation knowing what a gazetteer is -- and
 * until now its only implementations were fixtures, five places typed into a parity test. This is
 * the one that answers from the 42 400 places of a real corpus, and it lives HERE because the
 * arrow points this way: LplKnowledge depends on LplPlugin, never the reverse, so a reader of
 * `.lplknow` may implement an interface the engine declares.
 *
 * @warning **A place nobody has found on the ground gets no coordinates, and that is a feature.**
 * Pleiades marks 6502 places `unlocated` -- known from texts, never identified in the field --
 * and @ref history::Place carries `located` precisely so they can be carried without being
 * positioned. Inventing a position for them would turn "nobody knows where Cimmeria was" into an
 * epsilon somebody walks to.
 *
 * @warning Ring-0 safe, like the rest of `knowledge/`: it borrows a @ref KnowledgePack and allocates
 * nothing. What it cannot do is bisect -- see @ref PackPlaceResolver::resolve.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_KNOWLEDGE_PLACERESOLVER_HPP
#    define LPL_LPL_KNOWLEDGE_PLACERESOLVER_HPP

#    include <lpl/history/Place.hpp>
#    include <lpl/knowledge/KnowledgePack.hpp>

namespace lpl::knowledge {

/**
 * @class PackPlaceResolver
 * @brief @ref history::IPlaceResolver answered from a `.lplknow` image.
 */
class PackPlaceResolver final : public history::IPlaceResolver {
public:
    /**
     * @brief Binds an image.
     *
     * Held by POINTER and never copied: a catalogue of tens of thousands of places is exactly
     * the thing not to duplicate, and the image is memory a caller already owns.
     *
     * @param pack The corpus. Must outlive this.
     */
    void bind(const KnowledgePack &pack) noexcept { _pack = &pack; }

    /**
     * @brief Looks a place up.
     *
     * @warning A LINEAR scan, and deliberately so until measured otherwise: the gazetteer is not
     * sorted by identifier -- it is written in the order the reader met the places -- so
     * bisecting it would silently return the wrong entry rather than none. Sorting it at bake
     * time is the fix if a profile ever asks for one, and it is a change to the WRITER, not a
     * cleverer reader over unsorted data.
     *
     * @param id  The identifier.
     * @param out Receives it.
     * @return false when this corpus does not carry that place at all -- which is different from
     *         carrying it without a position.
     */
    [[nodiscard]] bool resolve(core::u32 id, history::Place &out) const override;

    /**
     * @brief Collects the places a corpus says this one is connected to.
     *
     * @warning Evidence, never geometry. A road, a sea lane or a pass is something a source states
     * and no distance measure can recover: the nearest place across a mountain range is not the
     * place anybody actually went to next.
     *
     * @param id       The place.
     * @param out      Receives the neighbours.
     * @param capacity Room in @p out.
     * @return How many were written.
     */
    [[nodiscard]] core::u32 linkedPlaces(core::u32 id, core::u32 *out, core::u32 capacity) const override;

    /**
     * @brief How many places the bound corpus carries.
     *
     * @return The count, or zero when nothing is bound.
     */
    [[nodiscard]] core::u32 placeCount() const noexcept;

private:
    const KnowledgePack *_pack{nullptr};
};

} // namespace lpl::knowledge

#endif // LPL_LPL_KNOWLEDGE_PLACERESOLVER_HPP
