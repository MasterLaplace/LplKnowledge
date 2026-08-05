-- Parity and property tests. The knowledge side has the same obligation as the
-- engine: what the host computes and what ring 0 reads must fold identically.
--
-- `test-graph-consensus` is GONE, and deliberately not replaced: the consensus it was
-- going to test is `lpl::history`'s, already exercised by gate P13 in LplPlugin, and its
-- module is folded (see graph/FOLDED.md). `test-mirror-taxonomy` is gone for the opposite
-- reason — `mirror/` is still scaffolding, and a target whose source is
-- `int main(){return 0;}` is a check that cannot fail. Each comes back on the commit that
-- gives it something to check, which is the rule LplPlugin already paid for once.

-- Identity, addressing and language tags. NO foundation required: none of it needs Fixed32,
-- so a standalone checkout of this repository still has something real to run — which is the
-- point of the standalone build being honest rather than stubbed.
target("test-corpus-identity")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    add_deps("lpl-corpus", "lpl-knowledge")
    add_files("test_corpus_identity.cpp")
target_end()

-- Reading a corpus into facts. No foundation either: turning text into a baker is string
-- handling, and the arithmetic of doubt lives in another repository.
target("test-harvest-ingest")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    -- lpl-foundation when it exists, and not because this test needs Fixed32 — it does not.
    -- lpl-harvest carries `bakeParityCorpus`, which reads the canonical corpus out of
    -- `lpl::history`, so the archive has an unresolved reference to it whether or not this
    -- particular consumer calls it. Without the foundation that symbol is not compiled at
    -- all and nothing is missing.
    add_deps("lpl-harvest", "lpl-knowledge", "lpl-corpus")
    if LPL_FOUNDATION_AVAILABLE then add_deps("lpl-foundation") end
    add_files("test_harvest_ingest.cpp")
target_end()

-- Gate P18 `corpus`. Declared only when the foundation is there, and that is
-- Foundation.hpp's own policy rather than an accident: without Fixed32 there is no
-- determinism contract to put under test, so the target is ABSENT rather than stubbed. A
-- gate that passes without having checked anything is worse than no gate.
if LPL_FOUNDATION_AVAILABLE then

target("test-knowledge-parity")
    set_kind("binary")
    set_group("tests")
    set_default(false)
    add_deps("lpl-knowledge", "lpl-corpus", "lpl-harvest", "lpl-foundation")
    add_files("test_knowledge_parity.cpp")
target_end()

end -- if LPL_FOUNDATION_AVAILABLE
