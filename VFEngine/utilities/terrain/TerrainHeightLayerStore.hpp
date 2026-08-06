#pragma once
#include "TerrainExport.hpp"

#include "SplineCorridorDeform.hpp"
#include "TerrainTypes.hpp"
#include <cstdint>
#include <functional>
#include <optional>
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

    // How many records one stack may hold. Lives here rather than in TerrainLayerSidecar.hpp --
    // where VK-1646 first put it -- because that header includes this one, so the store could not
    // see it. It is a store invariant; the sidecar writer merely refuses to persist a stack that
    // already broke it.
    //
    // NOT `MAX_TERRAIN_LAYERS`: that name is taken by the material palette's cap of 32
    // (TerrainMaterialTypes.hpp). The two "layers" are unrelated -- that one bounds how many
    // material entries a tile can blend, this one how many edit-layer records a stack may carry.
    inline constexpr uint32_t MAX_TERRAIN_EDIT_LAYERS = 4096;

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

        // VK-1647. Drops a coord from every layer's affected set. Pairs with eraseBase() on the
        // deliberate-deletion path, and exists to keep one invariant true: a coord a layer claims
        // always owns a base.
        //
        // Without it, deleting a tile left the coord claimed but baseless, which reads as COVERED
        // (isCovered is satisfied by either). recomposeTile then refuses it forever, while
        // normalizeDerivedSeams still welds its boundary column -- and a covered<->covered weld
        // averages in the neighbour's COMPOSED value, so another layer's corridor bleeds into a
        // plane no layer can rebuild. The state also survives a save: `affected` and the base index
        // are persisted independently and neither is validated against the other.
        //
        // Returns the number of layers that were carrying the coord.
        size_t dropCoordFromLayers(const TileCoord& coord);

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
        //
        // VK-1647 also refuses a duplicate id and an over-cap stack. Both used to be caught only by
        // the sidecar WRITER, which refuses the whole save -- so the terrain silently became
        // unsaveable, and a duplicate id made removeLayer/setLayerVisible (both first-match)
        // address the wrong record. Rejecting here keeps the in-RAM stack always persistable.
        bool addLayer(HeightLayerRecord record);
        bool removeLayer(uint64_t id);
        bool setLayerVisible(uint64_t id, bool visible);
        [[nodiscard]] const std::vector<HeightLayerRecord>& layers() const { return stack; }
        [[nodiscard]] bool hasLayer(uint64_t id) const;

        // VK-1647. Position of a layer in composition order, or nullopt when the id is unknown.
        [[nodiscard]] std::optional<size_t> layerIndex(uint64_t id) const;
        [[nodiscard]] const HeightLayerRecord* layer(uint64_t id) const;

        // Moves one layer to `newIndex`, sliding everything between the old and new position by one.
        // std::rotate, NOT a swap: a swap would move a second layer past the others as well, so an
        // artist dragging one row would silently reorder two. False on an unknown id or an index
        // past the end; a no-op move (newIndex == current) returns true and changes nothing.
        bool moveLayer(uint64_t id, size_t newIndex);

        // Replaces a layer's parameters and affected set IN PLACE, keeping its id and its stack
        // position. `eval` is rebuilt through makeSplineCorridorEval -- the one construction site --
        // so an edited layer cannot drift from one reloaded off disk.
        //
        // The caller owns invalidation, and must invalidate the OLD affected set as well as the new
        // one: tiles the edit narrowed off keep their (sticky) coverage and have to be recomposed
        // back to base + the remaining stack.
        //
        // `affected` REPLACES the old set rather than merging with it, so a caller that builds it
        // by filtering on residency will silently un-claim every unloaded part of the layer. See
        // the ApplySplineDeformCommand handler for what that costs and how it carries them over.
        //
        // Does not touch `visible`: this is a parameter update, and a caller that means "apply"
        // should say so separately.
        bool updateLayer(uint64_t id, SplineCorridorLayerParams params,
                         std::unordered_set<TileCoord, TileCoordHash> affected);

        // The tiles a moveLayer across the inclusive stack range [lo, hi] can change: the moved
        // layer's VISIBLE affected set intersected with the union of the crossed VISIBLE layers'
        // sets. Empty when the moved layer is hidden.
        //
        // It misses nothing. A tile outside the moved layer's set never lists it among its
        // contributors; a tile inside it but claimed by none of the crossed layers has the moved
        // layer sliding past layers absent from that tile's contributor list. Either way that tile
        // composes identically before and after, so recomposing it would be pure waste -- and on a
        // map where one long road crosses another once, the waste is the entire length of both.
        //
        // It is still a slight over-approximation, and unavoidably so: `affected` is per TILE while
        // commutativity is per VERTEX. Two roads crossing one tile without their bands overlapping
        // both claim it, so it enters the set, yet each layer is the identity outside its own band
        // and the tile recomposes to the same bytes. Tile granularity cannot see that.
        //
        // Callers still owe the seam ring: this is a set of CHANGED tiles, not of tiles to
        // invalidate.
        //
        // Empty when the id is unknown or the range is out of bounds. Callers still owe the seam
        // ring: this is a set of CHANGED tiles, not of tiles to invalidate.
        [[nodiscard]] std::unordered_set<TileCoord, TileCoordHash>
        reorderImpactSet(uint64_t id, size_t lo, size_t hi) const;

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

        // VK-1647. The raw set, for counting only. It promises NO order, which is exactly why it
        // does not weaken the rule above: anything whose result depends on visit order has to go
        // through staleSorted() and be seen to. Exists so a progress query does not have to
        // allocate and sort a vector every frame to learn how many tiles are outstanding.
        [[nodiscard]] const std::unordered_set<TileCoord, TileCoordHash>& staleCoords() const
        {
            return derivedStale;
        }

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

        // --- VK-1647 stable layer ids ---

        // Monotonic and NEVER reused, which is stronger than "not currently in use". A
        // RoadSplineComponent carries its splineId in the SCENE and outlives the layer it names, so
        // handing a freed id to a new layer would silently re-point a surviving road at someone
        // else's corridor. Persisted in the VFTL header for the same reason: without it a reload
        // restarts at 1 while the sidecar has just restored ids 1..N.
        [[nodiscard]] uint64_t peekNextLayerId() const { return nextLayerId; }
        uint64_t reserveLayerId() { return nextLayerId++; }

        // Raise-only, so no ordering between "load the sidecar" and "some id was already issued"
        // can lower the watermark.
        void adoptNextLayerId(uint64_t value)
        {
            if (value > nextLayerId)
                nextLayerId = value;
        }

    private:
        std::unordered_map<TileCoord, BaseHeightBlock, TileCoordHash> bases;
        std::unordered_set<TileCoord, TileCoordHash> derivedStale;
        std::vector<HeightLayerRecord> stack;
        bool editingLocked = false;

        // 1 rather than 0: id 0 is the "no spline" sentinel on RoadSplineComponent and on every
        // spline command that carries one.
        uint64_t nextLayerId = 1;
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
