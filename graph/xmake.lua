-- /////////////////////////////////////////////////////////////////////////////
-- @file xmake.lua
-- @brief Build configuration for the lpl::graph module.
-- graph/ build configuration — the temporal knowledge graph and its arithmetic of doubt.
-- Freestanding, and integer throughout: a confidence is Fixed32, not a float,
-- because two machines that disagree on the third decimal of a belief will
-- eventually disagree about what happened. This is where the project stops being
-- Laplace-the-determinist and becomes Laplace-the-probabilist — the two halves he
-- wrote in the same book.
-- /////////////////////////////////////////////////////////////////////////////

target("lpl-graph")
    set_kind("static")
    set_group("modules")
    add_deps("lpl-knowledge")
    add_includedirs("include", { public = true })
    add_files("src/**.cpp")
    add_headerfiles("include/(lpl/graph/**.hpp)")
target_end()
