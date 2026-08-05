/**
 * @file MotionTrace.hpp
 * @brief Reading motion vectors without decoding frames.
 *
 * The cheap path, and the one that makes a large archive tractable at all: scan
 * packets, watch vector magnitude, decode only at the troughs.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_MEDIA_MOTIONTRACE_HPP
#    define LPL_LPL_MEDIA_MOTIONTRACE_HPP

#    include <lpl/Foundation.hpp>

namespace lpl::media {

// TODO(lot 1): declarations only — no implementation yet.

} // namespace lpl::media

#endif // LPL_LPL_MEDIA_MOTIONTRACE_HPP
