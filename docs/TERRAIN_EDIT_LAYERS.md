# Terrain Edit Layers

Decision record for VK-1618, based on branch `VK-1618` at
`fa3f9b874cf2738de99233103bd762160473324c`.

> **Verdict:** defer a general UE5-style height/paint/hole edit-layer stack.
> Build a smaller, height-only parametric foundation for reserved spline/carve
> layers. This spike changes documentation and backlog only; it does not change
> engine code or the VFTR format.

The repository contains no `.vfterrain` asset that can serve as the requested
"real RTS map" measurement. All map totals below are therefore a modelled
budget for a 32×32-tile (1,024-tile) skirmish map. The RTS gameplay plugin does
consume terrain data for fog-of-war integration, but it does not provide a
terrain asset to measure.

---

## 1. Decision and boundaries

### Build

The first non-destructive terrain capability should support:

- ordered, stable-ID height spline/carve definitions;
- show/hide, reorder, delete, and deterministic re-evaluation;
- one authoritative sparse base-height block per affected tile;
- operation-level undo for spline stack changes;
- CPU composition into the existing terrain height buffer;
- persistence in an adjacent, independently versioned sidecar.

This is a parametric reserved-layer foundation, not serialized snapshots of
each spline's output. The current spline implementation captures the already
composited height buffer before each spline and restores it on deletion
(`SplineTerrainServiceImpl.cpp:142-162,176-190`). Persisting those snapshots
would preserve the existing stale-restore bug for overlapping splines.

### Defer

The following remain gated and are not ready implementation stories:

- freehand height edit layers;
- paint spline layers and general paint edit layers;
- hole, water, or arbitrary user-created stacks;
- GPU-only composition or preview;
- sub-tile/brick storage;
- unrestricted composition of more than eight material palette entries on one
  tile.

Paint is deliberately excluded from the reserved subset. Paint mode currently
creates no `SplineData` record (`SplineTerrainServiceImpl.cpp:132-139`), while
the flattened terrain format has eight float weight planes and an eight-entry
per-tile palette (`TerrainWeightMap.hpp:11,17-25`). Authoring layers could
eventually bake into those planes, but the engine first needs deterministic
conflict/reduction semantics when more than eight distinct materials survive
composition. Channel reassignment currently zeroes the least-used channel and
renormalizes the tile (`TerrainWeightMap.cpp:94-146`).

## 2. Current storage and memory budget

Terrain resolutions are 33, 65, and 129 vertices per side
(`TerrainTypes.hpp:12-19`). Heights are `float` per vertex, holes are one byte
per quad, and weights are eight `float` planes per vertex
(`TerrainTile.hpp:63-65`, `TerrainWeightMap.hpp:11,17-25`).

### Per-tile editable source bytes

| Resolution | Height | Holes | Eight weight planes | Combined upper bound |
|---|---:|---:|---:|---:|
| 33×33 | 4,356 | 1,024 | 34,848 | 40,228 |
| 65×65 | 16,900 | 4,096 | 135,200 | 156,196 |
| 129×129 | 66,564 | 16,384 | 532,512 | 615,460 |

The combined column is an upper bound for a hypothetical layer carrying all
three data kinds. It is not the cost of the proposed height-only reserved
layer.

### Height-only reserved base on a 1,024-tile map

Values are decimal MB and exclude hash-table/index overhead and spline control
points. The proposed model stores one base block for each affected tile,
regardless of how many splines overlap it.

| Occupancy | 33×33 | 65×65 | 129×129 |
|---|---:|---:|---:|
| 5% of tiles | 0.223 MB | 0.865 MB | 3.408 MB |
| 25% of tiles | 1.115 MB | 4.326 MB | 17.040 MB |
| 100% of tiles | 4.461 MB | 17.306 MB | 68.162 MB |

Tile granularity is the v1 choice because generic GPU brushes currently upload
and read back whole height arrays (`BrushComputePipeline.cpp:399-422,507-522`).
It is not a fundamental limitation: before/after arrays can be diffed, and
specialized operations already know affected regions. Brick storage is a
future optimization after dirty-region reporting is standardized.

The current flattened GPU weight arena remains a separate existing limit. It
contains 32 Mi `uint32_t` elements (128 MiB,
`TerrainMeshBuffer.cpp:37-45`). A fully initialized 129×129 tile uploads
133,128 bytes, or 33,282 arena elements, giving an ideal capacity of 1,008
tiles. A 32×32 map exceeds that only when all 1,024 weight maps are initialized
and resident; fragmentation can reduce practical capacity. Authoring-side
layers do not inherently multiply this arena because only their flattened
result is uploaded.

## 3. Authoritative data and CPU composition

### Ownership

For every tile touched by a reserved spline:

1. `baseHeight` is the authoritative artist-edited height plane.
2. The sidecar owns ordered spline definitions, each with a stable ID,
   parameters, visibility, and stack position.
3. `heightData` remains the derived, flattened result consumed by geometry,
   rendering, physics, streaming, and VFTR.
4. Re-evaluation starts from `baseHeight`, applies every enabled spline in
   deterministic stack order, then performs derived seam normalization.

There is one base block per affected tile, not one `originalHeights` snapshot
per spline. Adding, deleting, hiding, or reordering a spline invalidates only
its affected tiles and their seam-neighbor ring.

Ordinary sculpting on a covered tile must mutate `baseHeight`, not the
composite. GPU brush input/output is routed through the authoritative base for
that tile, after which the spline stack is re-evaluated. VK-1615 stroke undo
likewise snapshots/restores the base for covered tiles and then regenerates
the derived result. This prevents later spline changes from discarding ordinary
sculpt edits.

### Merge and regeneration

CPU composition is the canonical v1 path:

- rendering consumes baked vertex positions rather than a render-resident
  height texture (`resources/shaders/gpudriven/mesh_terrain.glsl:127-146`);
- the current GPU brush path performs upload, dispatch, blocking fence/readback,
  and whole-array replacement;
- CPU terrain generation, collision, and physics already consume `heightData`.

GPU preview can be evaluated later, but it cannot replace the CPU canonical
result without redesigning those consumers.

`TerrainGrid` regenerates at most eight ordinary dirty tiles per frame
(`TerrainGrid.cpp:160-163,214-240`). Invalidating 1,024 tiles therefore has a
minimum scheduling latency of 128 frames, about 2.13 seconds if the application
sustains 60 fps. Visibility/reorder operations that touch large regions must
be asynchronous and progress-reported. The preceding edge-sync loop is
unbudgeted, but it runs only for tiles explicitly marked `edgeSyncDirty`.

### Seam and undo rules

Current seam synchronization averages and writes both tiles. Reserved-layer
composition must treat those writes as derived output only:

- never copy seam-adjusted values back into `baseHeight`;
- recompose affected tiles before seam normalization;
- include the neighboring seam ring in invalidation;
- make repeated compose/undo/redo cycles byte-stable.

Spline add, delete, hide, and reorder undo records store the operation and
parameters, not merged tile endpoints. Undo/redo changes the ordered definition
set and triggers the same deterministic re-evaluation. Freehand base sculpting
continues to use bounded before/after tile snapshots because brush operations
are not generally invertible.

## 4. Persistence and compatibility

### Why a sidecar

VFTR is currently version 2.4.0 and rejects any non-identical version triple
(`TerrainSerializer.hpp:19-22`, `TerrainSerializerRead.cpp:32-44`). Its header
contains a variable-length material path before the index
(`TerrainSerializerRead.cpp:75-102`), the index offset is recovered from
`tellg()` rather than stored (`TerrainSerializerRead.cpp:260-288`), and each
tile index entry has a fixed 52-byte serialized layout
(`TerrainSerializer.hpp:94-113`).

An adjacent `.vfterrainlayers` file isolates authoring data from that index and
from VFTR's incremental append path. This does not make the sidecar free:
asset discovery, reference scanning, Save As, rename, delete, export, and
dependency packaging must all recognize it.

### VFTL v1 contract

The future sidecar implementation uses:

- magic `VFTL`, little-endian encoding, and an independent v1 version;
- terrain asset GUID plus grid bounds, tile resolution, and a 64-bit content
  hash of the associated flattened VFTR generation;
- ordered layer records with stable ID, type, visibility, order, parameters,
  and affected-tile references;
- a coordinate-keyed sparse tile index for authoritative base-height blocks;
- uncompressed `float` height blocks in v1;
- stored byte length and CRC32 for every indexed block.

VFTR bit 6 is reserved as `HAS_EDIT_LAYER_SIDECAR`. VK-1644 has since moved VFTR
to **2.5.0** (`TileIndexEntry` gained `payloadSize`, so the entry is 56 bytes),
and incremental flag computation now unions with the on-disk flags rather than
replacing them (`TerrainSerializer::mergeIncrementalFlags`), so the marker
already survives an incremental save without any further work.

Load behavior is deterministic:

- marker set and matching valid sidecar: enable reserved-layer editing;
- marker set but sidecar missing, corrupt, or hash-mismatched: load flattened
  VFTR for viewing/runtime, disable layer editing, and offer recovery;
- marker clear but a sidecar exists: ignore/quarantine it as an orphan and
  warn rather than silently applying it.

This section originally claimed that "older readers ignore unknown flag bits".
That is false for VFTR, which rejects any version triple that is not identical
to its own (`TerrainSerializerRead.cpp`) — there is no reader old enough to
encounter bit 6 and also new enough to open the file. What the claim was
reaching for still holds, and is what VK-1646 relies on: bit 6 changes nothing
about the header layout or the tile records, so a build that does not know the
bit reads the flattened terrain exactly as before. If such a build saves, its
flag recomputation clears bit 6 and the sidecar becomes an orphan; the content
hash then prevents a later editor from silently reapplying stale layer data.

### Save and recovery

Create complete VFTR and VFTL temporary outputs first. Hash the VFTR temporary
output into VFTL, replace VFTR, then replace VFTL. There is no cross-file atomic
rename, so a crash between replacements is detected by the marker/hash pair;
the flattened VFTR remains the recovery result and layer editing stays
disabled until repaired.

The gaps this section originally recorded — a full save that removed the live
file before renaming its temporary, an in-place header/index patch that a crash
could tear, unbounded incremental growth with no compaction, and an incremental
path that neither persisted `materialPath` nor refreshed `.vfmeta` — have all
been closed by VK-1643 and VK-1644. The current contract is:

- **Replacement.** `resource::replaceFileAtomically` flushes the temporary file
  to disk and then commits it with a single `MoveFileExW(MOVEFILE_REPLACE_EXISTING
  | MOVEFILE_WRITE_THROUGH)`. The destination name resolves to the complete old
  file until it resolves to the complete new one; it is never absent.
- **Incremental commit.** Records are appended past EOF, made durable, and then
  the exact header and index bytes are written to a `<path>.vftrj` journal —
  hash-covered, so "committed" is all-or-nothing — before the live file is
  patched. `TerrainSerializer::recoverPending()` replays a committed journal and
  discards an incomplete one; replay is idempotent, and it never truncates,
  because an unreferenced tail is inert and counts as obsolete instead.
- **Compaction.** Obsolete bytes are *derived*
  (`fileSize − header − index − Σ payloadSize`), so they are exact again after
  any interruption. Past `TERRAIN_COMPACT_MIN_OBSOLETE_BYTES` (1 MiB) and either
  a quarter of the live bytes or `TERRAIN_COMPACT_ABSOLUTE_BYTES` (64 MiB),
  `compact()` copies every live record verbatim into a new file and shifts its
  offsets. It needs no `TerrainGrid` and no tile residency, so — unlike a full
  save — it structurally cannot drop a streamed-out tile.
- **Ordering rule.** A full save and a compaction both delete any pending
  journal *before* the swap, never after: a journal describes the generation
  being replaced, and replaying it onto the new one would corrupt it.

## 4b. VK-1645 as built

VK-1645 implemented sections 3's ownership model. Four decisions were made during
implementation that this document did not anticipate.

### The store belongs to `TerrainGrid`, not to `TerrainTile`

Section 3 says "one base block per affected tile" without saying where it lives.
A `TerrainTile` member does not work: `TerrainGrid::removeTile` erases the owning
`unique_ptr`, and `TerrainService::streamOutTile` calls it. Because the base is
RAM-only until VK-1646, that destroys artist data silently.

The dirty flag does not protect it either. All four unload paths gate on
`TerrainFileCache::dirtyCoords`, and every save calls `refreshIndex`, which ends
in `dirtyCoords.clear()`. So *sculpt → save → walk away → stream out* is an
ordinary sequence that would lose the base with no error.

`TerrainHeightLayerStore` is therefore held by `TerrainGrid` and keyed by
`TileCoord`. Keying by coord also makes stream-in free: `addTileFromFile`
recreates a tile at the same coord and it re-attaches with no reattachment step
to forget. `TerrainFileCache` was rejected as owner because `fileCaches[...]` is
populated only on load, save, or entity remap — a freshly created, never-saved
terrain has no cache at all. Only `TerrainService::removeTile`, the deliberate
deletion, erases a base block.

`TerrainTile` is unchanged, deliberately: it is embedded across Graphics, the
physics collider path, the serializer and the tests.

### Mixed seams are welded ONE-SIDED

Section 3 says seam adjustment must never reach `baseHeight`. That is necessary
but not sufficient. Where a covered tile T neighbours an uncovered tile U, U's
derived plane *is* its authoritative plane, so averaging both sides — what
`syncBrushBoundaryHeights` does — mutates U's authority. And because T is rebuilt
from its base on every recompose while U is not, the average converges:
`(c+u)/2`, then `3c/4 + u/4`, then `7c/8 + u/8`. Hiding and re-showing a layer ten
times measurably rewrites ground the user never edited.

The rule is therefore conditional on coverage:

| Seam | Rule |
|---|---|
| uncovered ↔ uncovered | average both — unchanged, still owned by `syncBrushBoundaryHeights` |
| covered ↔ covered | average both derived planes |
| covered ↔ uncovered | **one-sided: the covered side conforms to the uncovered side** |

One-sided conformance satisfies both requirements at once, is geometrically
right (the uncovered neighbour is ground truth), needs no undo capture, and costs
nothing visually: a layer's affected set covers corridor **plus falloff**, so a
mixed seam always sits where the layer contributes ~0.

`syncBrushBoundaryHeights` now skips any pair where either side is covered, and
`TerrainGrid::normalizeDerivedSeams` owns the rest.

### The shared corner is the only non-idempotent seam vertex

Straight seams are bit-exactly idempotent: `(m + m) * 0.5f == m` in IEEE-754.
The corner is not. Within a single `syncBrushBoundaryHeights` call the `+X` pass
writes tile T's NE corner and the `+Z` pass then reads and rewrites that same
index, so one call leaves three different values at a 4-way corner and a second
call moves them again.

Recompose is byte-stable regardless, but only because of three rules that are now
load-bearing rather than incidental: compose always restarts from the base;
compose and seam welding are two strictly separated phases; and the seam pass
walks a `(z, x)`-sorted list. `TerrainHeightLayerStore::staleSorted()` is the only
accessor that hands out an ordering.

One consequence deserves naming: `recomposeDirtyDerived` recomposes every
**covered** member of the working ring, not only the stale seeds. Welding a
freshly composed tile against a covered neighbour still holding last cycle's
welded values would drift the shared corner on every pass. The budget therefore
bounds the seeds, and the real work is at most 5× that.

### Coverage is sticky

Section 3 implies coverage follows the layer set. It cannot: deleting the last
layer over a tile would hand authority back to the derived plane, and unless that
exact moment also collapses `heightData` to the base, the deleted spline's
corridor stays baked into derived forever.

`isCovered` is therefore true once the tile owns a base block, layer or no layer.
With an empty stack `composeTileHeights` yields `derived == base`, which is the
right answer and needs no transition logic at all. Coverage also ignores
`visible`, so hiding a layer cannot make the base stop being authoritative
mid-session.

### Scope actually shipped

- Spline replay **is** in VK-1645. Identity composition has no safe base seed:
  seeding post-spline bakes the corridor into the authority so VK-1647 would apply
  it twice, and seeding pre-spline makes the first sculpt dab visibly un-flatten
  the road. `SplineData::originalHeights` was removed outright.
- Spline apply undo/redo is a **layer visibility flip**, not a snapshot restore.
  Hiding rather than removing keeps coverage, so the tile's base stays
  authoritative and a sculpt underneath still routes there.
- Hydraulic erosion **refuses** to run on covered tiles. Its gather rests on
  "every owner of a seam vertex already holds the same value", which base-vs-derived
  breaks on a mixed rect. A delta-scatter formulation is filed as a follow-up.
- Runtime script edits stay on **derived**, deliberately. They are transient by
  design — no `markDirty`, no undo entry — so routing them to the base would let a
  gameplay crater permanently rewrite authored data with no way back.
- VFTR flag bit 6 was deliberately **left clear** at the time: nothing wrote a
  sidecar yet, and setting it would have dropped every terrain saved by that build
  into VK-1646's recovery path. VK-1646 now sets it. (Superseded — see §4c.)
- Byte-stability held **within a session with no intervening save**, because the base
  was RAM-only. VK-1646 persists it uncompressed, so it now survives a save too; the
  derived plane still round-trips through uint16 quantization.

## 4c. VK-1646 as built

Section 4's contract shipped essentially as written. Five things it did not
anticipate:

### The binding is a stored nonce, not a hash of anything

Section 4 specified "a 64-bit content hash of the associated flattened VFTR
generation". Implementation showed that to be wrong, in two ways.

First, a hash over only the header and index table does not work: a tile's height
payload is a fixed-size quantised block (`writeTileHeightData`), so two full saves
that differ only in sculpted heights produce a byte-identical header and index. The
most common edit there is would go undetected.

Second — and decisively — a hash over the *whole* file does not work either, because
two existing paths rewrite every byte of a `.vfterrain` without changing a single
height:

- `TerrainSerializer::compact()` relocates every record and shifts every offset.
- `AssetReferenceScanner::updateTerrainFile` splices a renamed material path into the
  header and adjusts the index, then swaps the file. It runs whenever a
  `.vfTerrainMat` is renamed.

Either would make a perfectly valid sidecar read as stale — and a false stale costs
the artist the entire layer stack. **Renaming a material must not delete an artist's
roads.**

So `TerrainFileHeader` gained `editLayerGenerationId`: an opaque random 64-bit value,
regenerated whenever the sidecar is rewritten and stamped into both files by the same
commit. Both paths above copy the header through verbatim, so it survives them for
free. Random rather than a counter, because a counter collides after a restore from
backup.

This also removes the reason to force a full save. Only the id's *value* changes on an
incremental save, and it is patched in place with the rest of the header — so a
layered terrain keeps using the incremental path.

### Bit 6 gates an 8-byte header block, and both transitions cost one full save

The id lives in the flag-gated tail beside the physics and streaming blocks, so a
terrain with no layers is byte-identical to one the previous build wrote and **no
version bump is needed** — 2.5.0 files stay readable.

The cost is that turning bit 6 on (first layer) or off (last layer removed) resizes
the header, which moves the index table, which an in-place incremental patch cannot
do. `validateIncrementalHeaderLayout` refuses and one full save happens.

`TerrainService::prepareSaveIncremental` **must** mirror that decision into its
candidate flags, exactly as it already mirrors the physics and streaming bits. This is
not an optimisation: `saveTerrainIncremental` answers `NeedsFullSave` by calling
`saveTerrain()` on the background thread with no `prepareSave()`, which writes only
resident tiles — with streaming on, missing the mirror deletes every unloaded tile.

### A record's parameters had to become part of the record

`HeightLayerRecord::eval` is a `std::function` and cannot be persisted. The record
gained `type` (`HeightLayerType`) and a typed `SplineCorridorLayerParams`, and
`makeSplineCorridorEval` became the single construction site for the callable —
shared by the spline-apply handler and the sidecar loader, so a layer rebuilt from
disk cannot drift from one applied live.

`affected` is persisted verbatim rather than recomputed from the polyline: the apply
path only claims tiles that were resident *with height data* at the time, so the set
is a function of residency as well as of geometry.

### A partially understood sidecar is not applied at all

An unrecognised layer type is skippable — each record stores its own byte length —
but the file is reported `Degraded` and **nothing** is loaded, not even the records
this build does understand. Half a stack composes ground the artist never authored,
and because coverage is sticky the tiles would stay authoritative afterwards.

### The sidecar does not ship

`.vfterrainlayers` is excluded from `packAssets` (`GameExporter.cpp`). It is authoring
state, the runtime consumes only the flattened VFTR, and a fully covered map's sidecar
runs to tens of megabytes. The consequence is that the load rule's "marker set,
sidecar absent" row must be **silent in archive mode** — in a shipped build that is
the expected state, not damage.

Orphans (a sidecar beside a terrain whose bit 6 is clear) are left on disk and warned
about, never renamed or deleted. A load path has no business destroying a file it
cannot identify, and the content browser shows `.vfterrainlayers` — like `.vfCollider`
and unlike `.vfmeta` — precisely so the user can act on that warning.

## 5. Follow-up backlog

VK-1619 already owns VFTR 2.4.0 round-trip coverage. It should be extended with
incremental dirty-tile preservation, unchanged-tile preservation, index-offset
integrity, and malformed/truncated-file rejection instead of creating a
duplicate serializer-test story.

| ID | Story | Size | Dependencies |
|---|---|---:|---|
| [VK-1644](https://matan33214.atlassian.net/browse/VK-1644) | VFTR crash-safe replacement and incremental-growth compaction | M | VK-1619 |
| [VK-1643](https://matan33214.atlassian.net/browse/VK-1643) | Persist incremental `materialPath` and refresh `.vfmeta` | S | VK-1619 |
| [VK-1645](https://matan33214.atlassian.net/browse/VK-1645) | Authoritative base-versus-derived height ownership and destructive-edit/undo integration | L | VK-1619 |
| [VK-1646](https://matan33214.atlassian.net/browse/VK-1646) | Versioned `.vfterrainlayers` format, asset lifecycle, and crash recovery | L | VK-1644, VK-1645 |
| [VK-1647](https://matan33214.atlassian.net/browse/VK-1647) | Deterministic parametric height-spline evaluator and sparse invalidation | M | VK-1645 |
| [VK-1648](https://matan33214.atlassian.net/browse/VK-1648) | Reserved height-layer list/hide/reorder/delete UI and operation-level undo | M | VK-1646, VK-1647 |

General paint/freehand/hole layers remain gated design work and are not filed
as ready stories.

## 6. Acceptance and validation

The reserved-layer follow-ups must cover:

- overlapping splines deleted and reordered out of application order;
- ordinary sculpt edits beneath an active spline followed by re-evaluation;
- repeated seam/corner compose and undo/redo without drift;
- visibility and reorder across unloaded/streamed tiles;
- missing, stale, truncated, and checksum-invalid sidecars;
- crashes between VFTR and VFTL replacement;
- Save As, rename, delete, reference scan, and game export;
- 5%, 25%, and 100% occupancy budgets;
- progress reporting for a 1,024-tile invalidation.

VK-1618 itself requires no build or runtime test. Validation is factual:
recheck source citations against the recorded commit, independently reproduce
the arithmetic, confirm the document is the only new tracked-work change, and
read back all Jira parents, sizes, links, and status transitions.
