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

-- `lpl-foundation` is linked when it exists, and that is not a convenience: the tools that need it
-- reach `lpl::history` — to read the corpus it declares, and to report the
-- consensus it computes — and those symbols have out-of-line code. Without the foundation
-- both still build, and both say plainly that the half needing Fixed32 is unavailable
-- rather than pretending to have produced it.
local kFoundationDeps = LPL_FOUNDATION_AVAILABLE and {"lpl-foundation"} or {}

target("lpl-ingest")
    set_kind("binary")
    set_group("apps")
    add_deps("lpl-harvest", "lpl-knowledge", "lpl-corpus", table.unpack(kFoundationDeps))
    add_files("harvest/main.cpp")
target_end()

target("lpl-knowbake")
    set_kind("binary")
    set_group("apps")
    add_deps("lpl-harvest", "lpl-knowledge", "lpl-corpus", table.unpack(kFoundationDeps))
    add_files("knowbake/main.cpp")
target_end()

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

target("lpl-ask")
    set_kind("binary")
    set_group("apps")
    add_deps("lpl-knowledge", "lpl-corpus", "lpl-harvest", table.unpack(kFoundationDeps))
    add_files("ask/main.cpp")
target_end()
