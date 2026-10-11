-- /////////////////////////////////////////////////////////////////////////////
-- @file xmake.lua
-- @brief Host-side command line tools.
--
-- None of these is ever linked into a kernel: they are the WRITERS, and a
-- constrained target is only ever a reader.
--
-- Naming follows LplKernel and LplPlugin: `lpl-` for EVERYTHING, libraries and
-- binaries alike. LplPlugin avoids collisions not by prefixing differently but by
-- naming its tools distinctly from its modules (`lpl-bake`, `lpl-mapview` — there is
-- no `bake` or `mapview` module). Same here: where a tool would have collided with
-- the library it links, the TOOL is renamed for what it does, and the library keeps
-- the module's name. Giving both the same name makes xmake report a circular
-- dependency.
-- /////////////////////////////////////////////////////////////////////////////

target("lpl-wikimirror")
    set_kind("binary")
    set_group("apps")
    add_deps("lpl-mirror")
    add_files("mirror/main.cpp")
target_end()

target("lpl-keyframe")
    set_kind("binary")
    set_group("apps")
    add_deps("lpl-media")
    add_files("keyframe/main.cpp")
target_end()

-- The tools that link `lpl-harvest` are declared with the foundation only, as harvest/ is. They
-- also link `lpl-foundation`, and that is not a convenience: they reach `lpl::history` — to read
-- the corpus it declares, and to report the consensus it computes — and those symbols have
-- out-of-line code.
if LPL_FOUNDATION_AVAILABLE then

target("lpl-tool-identity")
    set_kind("static")
    add_rules("laplace.identity")
    add_includedirs(".", {public = true})
    add_files("Identity.cpp")
target_end()

target("lpl-ingest")
    set_kind("binary")
    set_group("apps")
    add_deps("lpl-tool-identity", "lpl-harvest", "lpl-knowledge", "lpl-corpus", "lpl-foundation")
    add_files("harvest/main.cpp")
target_end()

target("lpl-knowbake")
    set_kind("binary")
    set_group("apps")
    add_deps("lpl-tool-identity", "lpl-harvest", "lpl-knowledge", "lpl-corpus", "lpl-foundation")
    add_files("knowbake/main.cpp")
target_end()

target("lpl-ask")
    set_kind("binary")
    set_group("apps")
    add_deps("lpl-tool-identity", "lpl-knowledge", "lpl-corpus", "lpl-harvest", "lpl-foundation")
    add_files("ask/main.cpp")
target_end()

end -- if LPL_FOUNDATION_AVAILABLE
