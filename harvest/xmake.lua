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

-- ⚠ libcurl rather than the `curl` binary. Both were tried, in that order, and the library wins
-- on three counts that are not style: no process per request (a harvest is thousands of them),
-- a REUSED connection to one repository instead of a fresh TLS handshake each page, and headers
-- read from the response rather than reconstructed from `--write-out`. `zlib` is on because an
-- OAI page is megabytes of XML and asking for an encoding the library cannot decode would be a
-- bug rather than an optimisation.
add_requires("libcurl", { configs = { zlib = true } })

target("lpl-harvest")
    set_kind("static")
    set_group("modules")
    add_deps("lpl-knowledge", "lpl-corpus")
    add_includedirs("include", { public = true })
    add_files("src/**.cpp")
    add_packages("libcurl", { public = true })
    add_headerfiles("include/(lpl/harvest/**.hpp)")
target_end()
