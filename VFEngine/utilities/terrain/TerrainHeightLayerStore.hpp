#pragma once
#include "TerrainExport.hpp"

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

    struct HeightLayerRecord
    {
        uint64_t id = 0;
        bool visible = true;
        HeightLayerTileEval eval;

        // Which tiles this layer covers. Independent of `visible` on purpose: hiding a layer
        // must not reclassify its tiles as uncovered, or the base would silently stop being
        // authoritative mid-session and the next sculpt dab would land on derived output.
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

        // Only for deliberate tile deletion. Eviction and stream-out must NOT call this.
        void eraseBase(const TileCoord& coord);

        void markBaseDirty(const TileCoord& coord);
        [[nodiscard]] bool isBaseDirty(const TileCoord& coord) const;
        void clearBaseDirty(const TileCoord& coord);

        // --- layer stack ---

        // An ORDERED vector, never an unordered container: composition is order-dependent, so
        // iteration order is part of the result.
        void addLayer(HeightLayerRecord record);
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

    private:
        std::unordered_map<TileCoord, BaseHeightBlock, TileCoordHash> bases;
        std::unordered_set<TileCoord, TileCoordHash> derivedStale;
        std::vector<HeightLayerRecord> stack;
    };

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
