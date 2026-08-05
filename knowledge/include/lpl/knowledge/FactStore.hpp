/**
 * @file FactStore.hpp
 * @brief Bounded, non-owning access to the facts in a pack.
 *
 * Returns views into the mapped image, never copies. A query that could return a
 * million rows returns a capped page and says so.
 *
 * The saying-so is the part that matters. A reader that silently truncated would make
 * "there are three claims about this king" and "there are three claims about this king
 * that fit in my buffer" the same answer, and only one of them is true. So a page
 * carries both numbers: what came back, and how many matched. The project has paid for
 * the other arrangement already — `query_entities` grew a `truncated` flag for exactly
 * this reason, and a silent cap is what §MES called a verification incapable of failing.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_KNOWLEDGE_FACTSTORE_HPP
#    define LPL_LPL_KNOWLEDGE_FACTSTORE_HPP

#    include <lpl/Foundation.hpp>
#    include <lpl/knowledge/KnowledgePack.hpp>
#    include <lpl/knowledge/Query.hpp>

namespace lpl::knowledge {

/**
 * Rows one page can hold.
 *
 * ⚠ A page is 32 bytes a row, so a page is two kibibytes. That is nothing on a host and
 * it is not nothing on a kernel stack — put one in BSS rather than in a frame, the same
 * way a 1024-entity scene had to be static before it stopped overflowing.
 */
inline constexpr core::u32 kMaxPageRows = 64u;

/**
 * @struct Page
 * @brief What came back, and what did not.
 */
struct Page {
    FactV1 rows[kMaxPageRows]{}; ///< The claims, in baked order.
    core::u32 count{0u};         ///< How many rows are filled.
    core::u32 matched{0u};       ///< How many claims matched, cap or no cap.
    core::u32 truncated{0u};     ///< 1 when @c matched exceeds @c count.
};

/**
 * @class FactStore
 * @brief Runs queries against one image.
 */
class FactStore {
public:
    /**
     * @brief Binds to an image.
     *
     * Holds a reference, not a copy: the pack itself already holds a pointer to bytes it
     * does not own, and a second layer of ownership would be a second lifetime to get
     * wrong.
     *
     * @param pack The image to interrogate. Must outlive this store.
     */
    explicit FactStore(const KnowledgePack &pack) noexcept : _pack(&pack) {}

    /**
     * @brief Answers one query.
     *
     * @param query What to look for.
     * @param out   Receives the page.
     */
    void run(const Query &query, Page &out) const noexcept;

    /**
     * @brief How many claims match, without collecting any of them.
     *
     * Separate from @ref run because the two questions have genuinely different answers
     * when a cap is involved, and a caller who only wants the count should not have to
     * provide two kibibytes of page to learn it.
     *
     * @param query What to look for.
     * @return The match count.
     */
    [[nodiscard]] core::u32 count(const Query &query) const noexcept;

    /**
     * @brief The claim a query holds most firmly.
     *
     * The default view over contradiction: where two sources cannot both be right, this
     * returns the better-supported one. It does NOT delete the other — the loser is
     * still in the image and still returned by @ref run, which is the whole point of
     * SIM-022. Ties go to the earlier baked row, so the answer does not depend on which
     * order a scan happened to take.
     *
     * @param query What to look for.
     * @param out   Receives the claim.
     * @return false when nothing matched.
     */
    [[nodiscard]] bool best(const Query &query, FactV1 &out) const noexcept;

    /**
     * @brief Folds a page into a signature.
     *
     * Field by field and row by row, so a query that starts returning claims in a
     * different order changes the signature. That is intended: the order IS part of the
     * answer, because @ref best breaks ties on it.
     *
     * @param page The page to fold.
     * @return The signature.
     */
    [[nodiscard]] static core::u32 foldPage(const Page &page) noexcept;

private:
    const KnowledgePack *_pack{nullptr};
};

} // namespace lpl::knowledge

#endif // LPL_LPL_KNOWLEDGE_FACTSTORE_HPP
