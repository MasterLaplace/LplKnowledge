-- /////////////////////////////////////////////////////////////////////////////
-- @file xmake.lua
-- @brief Build configuration for the lpl::knowledge module.
-- knowledge/ build configuration — the bounded reader over a baked knowledge image.
-- Freestanding. Zero allocation, bounds-checked, no libc: a knowledge pack is an
-- untrusted input and ring 0 is the reader least able to survive a bad one. Same
-- discipline as pack/: magic, version, declared size, section extents, content
-- hash, and an unknown section type is SKIPPED rather than fatal.
-- /////////////////////////////////////////////////////////////////////////////

target("lpl-knowledge")
    set_kind("static")
    set_group("modules")
    -- No add_deps on lpl-core / lpl-math: those targets belong to
    -- LplPlugin's xmake project. Their headers arrive through the
    -- root add_includedirs; how they LINK is decision 2 of
    -- LplKernel/docs/ARCHITECTURE_cible.md and is not settled here.
    add_includedirs("include", { public = true })
    add_files("src/**.cpp")
    -- The reader's queries and its provenance read dates in lpl::history's calendar, places through
    -- lpl::history::Place and relief through lpl::math's projection. A standalone build has none of
    -- the three, so it leaves them out rather than copy lpl::history: it keeps the image reader.
    if not LPL_FOUNDATION_AVAILABLE then
        remove_files("src/FactStore.cpp", "src/PlaceResolver.cpp", "src/Provenance.cpp",
                     "src/Query.cpp", "src/ReliefSource.cpp")
    end
    add_headerfiles("include/(lpl/knowledge/**.hpp)")
target_end()
