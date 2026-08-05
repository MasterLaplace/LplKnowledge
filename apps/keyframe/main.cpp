/**
 * @file main.cpp
 * @brief Extract the informative frames from a media archive.
 *
 * Built on an observation rather than a heuristic: a person explaining something
 * stops moving while the thing being explained is on screen. Motion troughs are
 * where the information is, and the encoded stream already carries the vectors
 * needed to find them — so decoding, the expensive step, is paid only for the
 * handful of frames actually kept.
 *
 * The body below is the API written from the caller's side, before the callee
 * exists — the cheapest way to find out whether a library is pleasant to use. It
 * is fenced out until the modules it names are implemented, and the entry point
 * fails loudly rather than returning success it has not earned.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include <lpl/Foundation.hpp>

#include <lpl/media/Index.hpp>
#include <lpl/media/Keyframe.hpp>
#include <lpl/media/MotionTrace.hpp>
#include <lpl/media/Transcript.hpp>

int main(int argc, char **argv)
{
    (void) argc;
    (void) argv;

#if 0 // ── intended usage ─────────────────────────────────────────────────────
    // The cheap path: scan packets, watch vector magnitude, never decode a frame.
    const lpl::media::MotionTrace trace = lpl::media::MotionTrace::scan(argv[1]);

    // A rise, then a fall, then stillness means an animation finished and the new
    // state is now readable. That curve is the trigger — not a fixed interval, which
    // yields blur and duplicates in equal measure.
    const auto moments = lpl::media::Keyframe::selectTroughs(trace, lpl::media::Keyframe::settled());

    lpl::media::Index index{argv[2]};
    index.setTranscript(lpl::media::Transcript::of(argv[1]));

    for (const auto moment : moments)
        index.add(moment, lpl::media::decodeSingleFrame(argv[1], moment));

    lpl::core::log::info("{}: kept {} frames of {} ({} per minute)", argv[1], moments.size(),
                         trace.frameCount(), index.framesPerMinute());
    return 0;
#endif // ─────────────────────────────────────────────────────────────────────

    LPL_NOT_IMPLEMENTED("lpl-keyframe");
}
