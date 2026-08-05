# `.lplknow` v1 — the baked knowledge image

Status: **specification stub**, lot 6.

The archival sibling of `.lplpak`, and deliberately the same shape, because one
format family means one reader discipline and one set of mistakes already made.

- `Header` — magic, version, declared size, content hash.
- `SectionEntry[]` — typed, offset and extent, **an unknown type is skipped**.
- `FactV1[]` — the sextuplets, sorted so a temporal query is a scan and not an index build.
- `SourceV1[]` — provenance and trust weights.
- `TextV1[]` — canonical text, addressed by URN.
- `IndexV1[]` — everything the host precomputed so the reader never has to.

Rules inherited from `.lplpak`, none of them negotiable: no `bool` on the wire,
every layout pinned by a `static_assert`, all confidences as raw Q16.16, and the
writer lives host-side while the reader must survive being handed garbage.
