#pragma once
#include "TerrainExport.hpp"

#include "SplineCorridorDeform.hpp"
#include "TerrainTypes.hpp"
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace terrain
{
#pragma warning(push)
#pragma warning(disable: 4251)

    // VK-1645. A reserved height layer's contribution to ONE tile.
    //
    // `in` and `out` never alias and `out` must be fully populated on every path, including
    // early-outs -- composeTileHeights relies on that to ping-pong buffers. The callable must be
    // pure and deterministic: it is replayed from the authoritative base on every recompose, and
    // any hidden state would break byte-stability across undo/redo cycles.
    //
    // Injected from Services as a std::function, the same shape as
    // TerrainGrid::setHeightSampler. Terrain.dll only CALLS it, so EventDispatcher -- which is
    // per-binary and would resolve to a private instance inside this DLL -- is never reached
    // from here.
    using HeightLayerTileEval = std::function<void(
        const TileCoord& coord,
        const TerrainTileConfig& config,
        const std::vector<float>& in,
        std::vector<float>& out)>;

    // VK-1646. What kind of contribution a layer makes, and therefore how to read its parameters
    // back off disk. Values are part of the VFTL on-disk contract and must never be renumbered.
    enum class HeightLayerType : uint32_t
    {
        Unknown = 0,        // in-memory only, or a type written by a newer build
        SplineCorridor = 1, // parameters live in HeightLayerRecord::spline
    };

    struct HeightLayerRecord
    {
        uint64_t id = 0;
        bool visible = true;
        HeightLayerTileEval eval;

        // VK-1646. The serializable half of the record. `eval` is a std::function and cannot be
        // persisted, so the sidecar stores `type` + the matching parameter struct and rebuilds
        // `eval` from them on load via makeSplineCorridorEval() below.
        //
        // A record left at Unknown still composes normally in RAM — `eval` is what compose reads —
        // but it cannot be written to a sidecar, and the writer refuses rather than silently
        // dropping a layer the artist can see.
        HeightLayerType type = HeightLayerType::Unknown;
        SplineCorridorLayerParams spline; // meaningful only when type == SplineCorridor

        // Which tiles this layer covers. Independent of `visible` on purpose: hiding a layer
        // must not reclassify its tiles as uncovered, or the base would silently stop being
        // authoritative mid-session and the next sculpt dab would land on derived output.
        //
        // Persisted verbatim rather than recomputed: the apply path only claims tiles that were
        // resident WITH height data at the time (TerrainServiceHandlers.cpp), so this set is a
        // function of residency as well as of geometry, and re-deriving it from the polyline
        // would silently widen a layer's reach across a reload.
        std::unordered_set<TileCoord, TileCoordHash> affected;
    };

    // The authoritative, artist-owned height plane for one covered tile. TerrainTile::heightData
    // is derived output recomputed from this; nothing may ever copy derived values back in here.
    struct BaseHeightBlock
    {
        std::vector<float> heights;
        uint32_t vertexCount = 0; // guards a tile rebuilt at a different resolution
        bool dirty = false;       // authoritative bytes changed since last persist (VK-1646 hook)
    };

    // Owned by TerrainGrid, keyed by TileCoord rather than by TerrainTile*.
    //
    // That key choice is load-bearing. The base has no persistence until VK-1646, so losing it
    // loses artist data -- and TerrainGrid::removeTile destroys the whole TerrainTile, which a
    // tile-resident member could not survive. Keying by coord also means a streamed-back tile
    // re-attaches to its base for free: there is no reattachment step to forget.
    class VF_TERRAIN_API TerrainHeightLayerStore
    {
    public:
        // --- authoritative base blocks ---

        [[nodiscard]] bool hasBase(const TileCoord& coord) const;
        [[nodiscard]] BaseHeightBlock* base(const TileCoord& coord);
        [[nodiscard]] const BaseHeightBlock* base(const TileCoord& coord) const;

        // Creates the block from `seed` on first call and returns the existing one afterwards --
        // seeding is a one-shot, so a second spline over the same tile cannot overwrite the
        // pre-first-spline ground with an already-composited plane.
        BaseHeightBlock& adoptBase(const TileCoord& coord, const std::vector<float>& seed,
                                   uint32_t vertexCount);

        // Move overload, for the sidecar loader: a fully covered High-resolution map carries ~68 MB
        // of base heights, and copying every block once more on the way in is pure waste. Live
        // callers pass an lvalue (a tile's height plane, which they keep) and still bind to the
        // const& form above, so no existing call site changes meaning.
        BaseHeightBlock& adoptBase(const TileCoord& coord, std::vector<float>&& seed,
                                   uint32_t vertexCount);

        // Enumeration for persistence. Deliberately the only way to walk the bases: everything
        // else addresses one coord at a time, which is what keeps callers from iterating in
        // hash order where an ordering is load-bearing (see staleSorted()).
        [[nodiscard]] const std::unordered_map<TileCoord, BaseHeightBlock, TileCoordHash>&
        allBases() const { return bases; }

        // Only for deliberate tile deletion. Eviction and stream-out must NOT call this.
        void eraseBase(const TileCoord& coord);

        void markBaseDirty(const TileCoord& coord);
        [[nodiscard]] bool isBaseDirty(const TileCoord& coord) const;
        void clearBaseDirty(const TileCoord& coord);

        // --- layer stack ---

        // An ORDERED vector, never an unordered container: composition is order-dependent, so
        // iteration order is part of the result.
        //
        // Returns false while editing is locked (VK-1646): a terrain whose sidecar is missing,
        // corrupt or stale must not accumulate layers over a base it does not have, because the
        // next save would then write a sidecar describing ground the artist never authored.
        bool addLayer(HeightLayerRecord record);
        bool removeLayer(uint64_t id);
        bool setLayerVisible(uint64_t id, bool visible);
        [[nodiscard]] const std::vector<HeightLayerRecord>& layers() const { return stack; }
        [[nodiscard]] bool hasLayer(uint64_t id) const;

        // Covered == this tile owns an authoritative base, or some layer claims it (visible or
        // not). STICKY: coverage survives deleting the last layer over the tile. See the .cpp.
        [[nodiscard]] bool isCovered(const TileCoord& coord) const;

        // --- derived invalidation ---

        void markDerivedStale(const TileCoord& coord);
        [[nodiscard]] bool isDerivedStale(const TileCoord& coord) const;
        void clearDerivedStale(const TileCoord& coord);
        [[nodiscard]] size_t staleCount() const { return derivedStale.size(); }

        // Sorted by (z, x). The ONLY accessor that hands out an ordering, because seam welding
        // at a shared corner is order-dependent -- see TerrainGrid::normalizeDerivedSeams.
        [[nodiscard]] std::vector<TileCoord> staleSorted() const;

        // True when nothing is covered and no layer exists: the whole feature is dormant and
        // every path can take its pre-VK-1645 shape.
        [[nodiscard]] bool empty() const { return stack.empty() && bases.empty(); }

        [[nodiscard]] size_t baseCount() const { return bases.size(); }

        // --- VK-1646 editing lock ---

        // Set when a terrain claims a sidecar (VFTR flag bit 6) that could not be loaded: missing,
        // corrupt, or bound to a different VFTR generation. The terrain still loads and renders
        // from its flattened heights; only layer AUTHORING is refused, because the authoritative
        // bases those layers would compose over are exactly what could not be read.
        //
        // Nothing here guards adoptBase(). It does not need to: every route to it is gated on
        // isCovered() (see TerrainService::authoritativeHeights), and a locked store is empty, so
        // no tile is covered and the sculpt path keeps treating the derived plane as authoritative
        // — which is the correct pre-VK-1645 behaviour for a terrain with no usable bases.
        void setEditingLocked(bool locked) { editingLocked = locked; }
        [[nodiscard]] bool isEditingLocked() const { return editingLocked; }

    private:
        std::unordered_map<TileCoord, BaseHeightBlock, TileCoordHash> bases;
        std::unordered_set<TileCoord, TileCoordHash> derivedStale;
        std::vector<HeightLayerRecord> stack;
        bool editingLocked = false;
    };

    // Builds the callable for a SplineCorridor layer. The ONE construction site for that semantic:
    // the spline-apply command handler and the sidecar loader both call it, so a layer rebuilt from
    // disk cannot drift from one applied live.
    //
    // Declared here rather than in SplineCorridorDeform.hpp because HeightLayerTileEval is defined
    // here, and this header already depends on that one — the reverse would be a cycle.
    //
    // The returned closure captures `params` by value and calls only pure Terrain.dll code, so it
    // never reaches back into Services or the (per-binary) EventDispatcher.
    [[nodiscard]] VF_TERRAIN_API HeightLayerTileEval makeSplineCorridorEval(
        SplineCorridorLayerParams params);

    // Pure: out = base, then every VISIBLE layer that covers `coord`, in stack order. Never
    // writes `base`.
    //
    // Always restarting from `base` -- rather than refining the previous derived plane -- is what
    // makes repeated compose cycles bit-identical. The corridor evaluator is not idempotent in
    // its falloff band, so composing on top of its own output would drift on every cycle.
    VF_TERRAIN_API void composeTileHeights(
        const TileCoord& coord,
        const TerrainTileConfig& config,
        const std::vector<float>& base,
        const std::vector<HeightLayerRecord>& layers,
        std::vector<float>& out);

#pragma warning(pop)
}
