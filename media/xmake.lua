-- /////////////////////////////////////////////////////////////////////////////
-- @file xmake.lua
-- @brief Build configuration for the lpl::media module.
-- media/ build configuration — extracting the substance of a video without watching it.
-- HOST ONLY. Built on an observation rather than a heuristic: a person explaining
-- something stops moving while the thing being explained is on screen. Motion
-- troughs are therefore where the information is, and the encoded stream already
-- carries the motion vectors needed to find them — so the expensive step, decoding,
-- is only paid for the handful of frames actually kept.
-- /////////////////////////////////////////////////////////////////////////////

target("lpl-media")
    set_kind("static")
    set_group("modules")
    add_deps("lpl-corpus")
    add_includedirs("include", { public = true })
    add_files("src/**.cpp")
    add_headerfiles("include/(lpl/media/**.hpp)")
target_end()
