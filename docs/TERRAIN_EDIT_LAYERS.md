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

VFTR bit 6 is reserved as `HAS_EDIT_LAYER_SIDECAR`; the VFTR version remains
2.4.0. Future flag computation must explicitly preserve/set the marker.

Load behavior is deterministic:

- marker set and matching valid sidecar: enable reserved-layer editing;
- marker set but sidecar missing, corrupt, or hash-mismatched: load flattened
  VFTR for viewing/runtime, disable layer editing, and offer recovery;
- marker clear but a sidecar exists: ignore/quarantine it as an orphan and
  warn rather than silently applying it.

Older readers ignore unknown flag bits and can still consume the flattened
terrain. If an older editor saves it, its current flag recomputation clears bit
6; the content hash prevents a later editor from silently reapplying stale
layer data.

### Save and recovery

Create complete VFTR and VFTL temporary outputs first. Hash the VFTR temporary
output into VFTL, replace VFTR, then replace VFTL. There is no cross-file atomic
rename, so a crash between replacements is detected by the marker/hash pair;
the flattened VFTR remains the recovery result and layer editing stays
disabled until repaired.

The existing full-save path is only a temporary-file replacement, not a truly
atomic replacement: it removes the live file before renaming the temporary
file (`TerrainSerializerSave.cpp:330-351`). Incremental save appends fresh
dirty-tile payloads at EOF and then rewrites the header/index in place
(`TerrainSerializerSave.cpp:193-208`). Full saves naturally reclaim orphaned
incremental payloads, but there is no threshold-triggered compaction and a
crash during the in-place patch can corrupt the live file.

Incremental save also copies the cached header and overwrites only flags,
physics, and streaming configuration (`TerrainSerializerSave.cpp:200-203`).
Consequently, a changed `materialPath` is not persisted incrementally, and the
incremental path does not refresh `.vfmeta`.

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
