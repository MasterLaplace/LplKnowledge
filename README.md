# LplKnowledge

The memory of the Laplace project: corpus, index, temporal graph, archive.

## Why it exists

A Laplace demon is defined by two things in equal measure: the laws that govern its world, and what
it knows. [LplKernel](https://github.com/MasterLaplace/LplKernel) is the machine,
[LplPlugin](https://github.com/MasterLaplace/LplPlugin) the world and
[LplAssistant](https://github.com/Christian-guajardo/LplAssistant) the voice; this repository is what
they know.

It follows LplPlugin's dual contract, along one border: the reader and the writer. Harvesting the
world's archives needs a heap, a network and text parsing; reading what was harvested needs none of
them, and the demon in ring 0 is the least capable reader there is. So the writers are host tools,
and the reader is a bounded, freestanding library that the kernel links as `libknowledge.a`.

It is written in C, C++ and assembly only. The reference implementations of this field are in Python
and held back by it; rewriting them natively is not a matter of style, it is what makes the scale
affordable.

## A first result in five minutes

The repository builds and tests on its own, without LplPlugin or LplKernel:

```sh
xmake f --root --foundation=detect   # uses LplPlugin when it sits next to this repository
xmake --root
xmake run test-corpus-identity       # identity, addressing, the reader's refusals
```

With LplPlugin next to it, `xmake run test-knowledge` also runs gate P18 (below).

Then bake a real corpus and ask it something. The LplKernel book carries 95 footnotes, each scoped
by its chapter:

```sh
xmake run lpl-ingest book.lplknow ~/LplKernel
xmake run lpl-ask book.lplknow --define 'LplKernel/docs/Books/LplKernel_Book.md#ch5[^19]'
```

```
LplKernel/docs/Books/LplKernel_Book.md#ch5[^19]
   J. Evans, « A Scalable Concurrent malloc(3) Implementation for FreeBSD », BSDCan 2006, …
  defined in LplKernel/docs/Books/LplKernel_Book.md … line 731
  cited 1 time
    LplKernel/docs/Books/LplKernel_Book.md … line 337
```

Every tool explains itself with `--help`, and `lpl-ask <image>` alone prints what an image holds.

## Where things live

| Module | Target | Role |
|---|---|---|
| `knowledge/` | freestanding | the bounded reader of a `.lplknow` image, its queries, its provenance |
| `corpus/` | freestanding | identity and addressing of source texts (URN/CTS, locus, language) |
| `harvest/` | host | the readers of real corpora, and the bakers that write images |
| `mirror/` | host | offline mirrors of reference documentation |
| `media/` | host | the substance of a video, extracted without watching it |

`knowledge/`, `corpus/` and `harvest/` are implemented and tested; `mirror/` and `media/` are still
scaffolding, and each file not yet written says why it exists.

| Tool | What it does |
|---|---|
| `lpl-ingest` | bakes an image from documents, or a catalogue of holdings from an archive |
| `lpl-knowbake` | bakes the parity corpus, the byte arrays the kernel embeds, and relief tiles |
| `lpl-ask` | queries one image, or several at once, from a terminal |
| `lpl-wikimirror` | builds the offline documentation mirror |
| `lpl-keyframe` | extracts the informative frames of a media archive |

**Building without the foundation.** `--foundation` takes `detect` (the default: use LplPlugin when it
is there), `off` (a host-only build) or `force` (fail when LplPlugin is missing, for CI). The words
are not `auto|y|n` on purpose: xmake turns option values that look like booleans into booleans, so
`n` and `auto` were both stored as `false`, and every "standalone" build quietly found the
foundation. `include/lpl/Foundation.hpp` is the one place that knows the difference.

| Mode | What is available |
|---|---|
| foundation present (`LPL_HAS_FOUNDATION`) | the determinism contract, and a `-ffreestanding` build |
| standalone | host only; Fixed32 is not emulated |

**What `lpl-ingest` reads.** Every document where it lives. It skips build output, generated views
and cloned third-party repositories: without those exclusions it once read 711 documents for the same
knowledge, 87 % of them noise.

## Gate P18 `corpus`

The canonical corpus of gate P13 history is baked into a `.lplknow` image, reopened, and the history
is rebuilt from what came back. Its three history signatures must equal gate P13's, on the host and
in ring 0, where `libknowledge.a` is the kernel's fifth library.

What it proves is a translation, not a computation: an image that opens cleanly but rounded a
confidence would pass everything else, and the consequence would be a different consensus on the
death of a king.

```sh
xmake run test-corpus-identity     # identity, addressing, the reader's refusals (no foundation needed)
xmake run test-harvest-ingest      # reading a corpus (no foundation needed)
xmake run test-knowledge           # gate P18: the round trip loses nothing, as in ring 0
xmake run test-parity-bake         # and the image the kernel embeds is the one the writer bakes
```

## What it refuses to do

- **Emulate Fixed32 in a standalone build.** A fake fixed point would let a standalone build claim a
  parity it cannot have.
- **Guess claims from prose.** Reading is structural: which stable identifiers exist, where each is
  defined, who cites it, the dates its headings carry. Pulling claims out of prose needs a model, and
  `harvest::Synthesis` takes one only to propose; a proposal becomes a fact once its words are found
  in the source.
- **Keep a second copy of `lpl::history`.** `graph/` described what LplPlugin's `history/` already
  implements and gates; it was folded, and [graph/FOLDED.md](graph/FOLDED.md) gives the reason file by
  file. The border is the one `history/Fact.hpp` draws itself: it trades in identifiers, and the
  strings live here.
- **Ship a stub that pretends.** A file not yet written carries its reason and a `TODO`, never a
  function that returns an unearned success.

## Licence

MIT. Author: [@MasterLaplace](https://github.com/MasterLaplace).
