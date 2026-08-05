-- /////////////////////////////////////////////////////////////////////////////
-- @file xmake.lua
-- @brief Build configuration for the lpl::harvest module.
-- harvest/ build configuration — ingestion: turning the world's open archives into a baked image.
-- HOST ONLY, and unapologetically so. This half allocates, parses text, speaks
-- HTTP and holds a database. It is the writer; nothing here is ever linked into a
-- kernel. Written natively rather than in a scripting language because the corpora
-- are measured in hundreds of millions of records, and because the reference
-- implementations being slow is precisely the gap worth closing.
-- /////////////////////////////////////////////////////////////////////////////

target("lpl-harvest")
    set_kind("static")
    set_group("modules")
    add_deps("lpl-knowledge", "lpl-corpus")
    add_includedirs("include", { public = true })
    add_files("src/**.cpp")
    add_headerfiles("include/(lpl/harvest/**.hpp)")
target_end()
