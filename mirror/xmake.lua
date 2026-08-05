-- /////////////////////////////////////////////////////////////////////////////
-- @file xmake.lua
-- @brief Build configuration for the lpl::mirror module.
-- mirror/ build configuration — local, offline mirrors of reference documentation.
-- HOST ONLY. The first vertical slice of the whole knowledge stack, chosen because
-- it exercises every stage — acquisition, streamed parsing, taxonomy extraction,
-- rendering, search — on a corpus small enough to finish. It is also immediately
-- useful: kernel work happens on machines that are deliberately offline.
-- /////////////////////////////////////////////////////////////////////////////

target("lpl-mirror")
    set_kind("static")
    set_group("modules")
    add_deps("lpl-corpus")
    add_includedirs("include", { public = true })
    add_files("src/**.cpp")
    add_headerfiles("include/(lpl/mirror/**.hpp)")
target_end()
