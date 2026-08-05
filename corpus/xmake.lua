-- /////////////////////////////////////////////////////////////////////////////
-- @file xmake.lua
-- @brief Build configuration for the lpl::corpus module.
-- corpus/ build configuration — identity and addressing for source texts.
-- Freestanding. A canonical identifier is what lets a claim point at the exact
-- line that supports it, forever. Without it, provenance degrades into a book
-- title and the whole epistemology collapses into trust-me.
-- /////////////////////////////////////////////////////////////////////////////

target("lpl-corpus")
    set_kind("static")
    set_group("modules")
    add_deps("lpl-knowledge")
    add_includedirs("include", { public = true })
    add_files("src/**.cpp")
    add_headerfiles("include/(lpl/corpus/**.hpp)")
target_end()
