-- Parity and property tests. The knowledge side has the same obligation as the
-- engine: what the host computes and what ring 0 reads must fold identically.
--
-- `test-graph-consensus` is GONE, and deliberately not replaced: the consensus it was
-- going to test is `lpl::history`'s, already exercised by gate P13 in LplPlugin, and its
-- module is folded (see graph/FOLDED.md). `test-mirror-taxonomy` is gone for the opposite
-- reason — `mirror/` is still scaffolding, and a target whose source is
-- `int main(){return 0;}` is a check that cannot fail. Each comes back on the commit that
-- gives it something to check, which is the rule LplPlugin already paid for once.

-- Identity, addressing, language tags and the image reader's refusals. NO foundation required:
-- none of it needs lpl::history or lpl::math, so a standalone checkout of this repository still
-- has something real to run — which is the point of the standalone build being honest rather
-- than stubbed. Its query checks read lpl::history's calendar, so they need the foundation.
target("test-corpus-identity")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    add_deps("lpl-corpus", "lpl-knowledge")
    add_files("test_corpus_identity.cpp")
target_end()

-- Everything below links lpl-harvest, which is declared with the foundation only (see
-- harvest/xmake.lua): its readers date what they read in lpl::history's calendar.
if LPL_FOUNDATION_AVAILABLE then

-- Reading a corpus into facts. Turning text into a baker is string handling, and the
-- arithmetic of doubt lives in another repository.
target("test-harvest-ingest")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    -- lpl-foundation, and not because this test needs Fixed32 — it does not. lpl-harvest
    -- carries `bakeParityCorpus`, which reads the canonical corpus out of `lpl::history`, so
    -- the archive has an unresolved reference to it whether or not this particular consumer
    -- calls it.
    add_deps("lpl-harvest", "lpl-knowledge", "lpl-corpus", "lpl-foundation")
    add_files("test_harvest_ingest.cpp")
target_end()

-- Deciding that two names denote one thing, and refusing to when nothing says so. Every
-- number here is a raw Q16.16 integer: the lock on the whole harvest needs no Fixed32.
target("test-entity-resolution")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    add_deps("lpl-harvest", "lpl-knowledge", "lpl-corpus", "lpl-foundation")
    add_files("test_entity_resolution.cpp")
target_end()

-- Reading prose into claims, and refusing what the prose did not say. No model: `IClaimReader`
-- exists so the half that CHECKS can be exercised without the half that guesses, and a safety
-- argument that could only be re-run behind an inference pass is one nobody re-runs.
target("test-synthesis")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    add_deps("lpl-harvest", "lpl-knowledge", "lpl-corpus", "lpl-foundation")
    add_files("test_synthesis.cpp")
target_end()

-- Reading a research run into the library. A report is text, and the arithmetic of doubt
-- lives in another repository.
target("test-harvest-research")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    add_deps("lpl-harvest", "lpl-knowledge", "lpl-corpus", "lpl-foundation")
    add_files("test_harvest_research.cpp")
target_end()

-- Reading a canonical text out of TEI. This is XML scanning and ordinal arithmetic, and it is
-- the FIRST producer in the repository of a three-level locus — the addressing `corpus/` was
-- designed around and that nothing had ever written.
-- @warning Every date shape here was measured in the corpus before it was written down: a date
-- parser tested against dates its author imagined is a parser tested against its author.
-- @warning The soft join. It may fill a value; it may never decide an identity, because splitting is
-- reversible and merging is not.
target("test-author-dates")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    add_deps("lpl-harvest", "lpl-knowledge", "lpl-corpus", "lpl-foundation")
    add_files("test_author_dates.cpp")
target_end()

-- @warning A headerless format cannot be validated from the inside, so every fixture here encodes its
-- own position: a misread of row order, column order or byte order shows up as a WRONG PLACE.
target("test-relief")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    add_deps("lpl-harvest", "lpl-knowledge", "lpl-corpus", "lpl-foundation")
    add_files("test_relief.cpp")
target_end()

target("test-mentions")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    add_deps("lpl-harvest", "lpl-knowledge", "lpl-corpus", "lpl-foundation")
    add_files("test_mentions.cpp")
target_end()

target("test-harvest-tei")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    add_deps("lpl-harvest", "lpl-knowledge", "lpl-corpus", "lpl-foundation")
    add_files("test_harvest_tei.cpp")
target_end()

-- Indexing what exists without fetching it. A holdings row is columns and positions, and the
-- arithmetic of doubt is not involved in saying who has a book.
target("test-catalogue")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    add_deps("lpl-harvest", "lpl-knowledge", "lpl-corpus", "lpl-foundation")
    add_files("test_catalogue.cpp")
target_end()

-- Gate P18 `corpus`. Declared only when the foundation is there, and that is
-- Foundation.hpp's own policy rather than an accident: without Fixed32 there is no
-- determinism contract to put under test, so the target is ABSENT rather than stubbed. A
-- gate that passes without having checked anything is worse than no gate.
--
-- Every LPL_TEST of tests/<module>/, in KTAP. A debug LplKernel runs the same sources in ring 0,
-- and its records must equal these. `xmake run test-knowledge 'corpus.*'` runs one suite.
target("test-knowledge")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    add_deps("lpl-testing", "lpl-knowledge", "lpl-corpus", "lpl-foundation")
    add_files("*/*.cpp")
target_end()

-- The writer's half of the gate: the canonical bake, and the image the kernel embeds is that bake.
target("test-parity-bake")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    add_deps("lpl-knowledge", "lpl-corpus", "lpl-harvest", "lpl-foundation")
    add_files("test_parity_bake.cpp")
target_end()

-- ⚠ Offline by construction. Every response this exercises is canned, because the part of an
-- OAI-PMH client with bugs in it is the loop — continuation, retry, tombstones, repeated tokens
-- — and each is a server behaviour that cannot be summoned from a live repository on demand.
target("test-oaipmh")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    add_deps("lpl-harvest", "lpl-knowledge", "lpl-corpus", "lpl-foundation")
    add_files("test_oaipmh.cpp")
target_end()

-- ⚠ The Pleiades reader was verified against 42 400 real places and guarded against nothing.
-- A component with no test is the one every future bug gets blamed on — a lesson this repository
-- paid for the same day, on the ECS double buffering.
target("test-gazetteer")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    add_deps("lpl-harvest", "lpl-knowledge", "lpl-corpus", "lpl-foundation")
    add_files("test_gazetteer.cpp")
target_end()

end -- if LPL_FOUNDATION_AVAILABLE
