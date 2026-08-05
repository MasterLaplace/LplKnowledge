/**
 * @file Changefile.hpp
 * @brief Daily differential synchronisation.
 *
 * Upsert on a stable identifier. Records migrate between partitions, so a naive
 * append leaves duplicates that quietly corrupt every count downstream.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_CHANGEFILE_HPP
#    define LPL_LPL_HARVEST_CHANGEFILE_HPP

#    include <lpl/Foundation.hpp>

namespace lpl::harvest {

// TODO(lot 2): declarations only — no implementation yet.

} // namespace lpl::harvest

#endif // LPL_LPL_HARVEST_CHANGEFILE_HPP
