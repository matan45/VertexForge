#include "TerrainHeightLayerStore.hpp"

#include <algorithm>

namespace terrain
{
    bool TerrainHeightLayerStore::hasBase(const TileCoord& coord) const
    {
        return bases.find(coord) != bases.end();
    }

    BaseHeightBlock* TerrainHeightLayerStore::base(const TileCoord& coord)
    {
        auto it = bases.find(coord);
        return it != bases.end() ? &it->second : nullptr;
    }

    const BaseHeightBlock* TerrainHeightLayerStore::base(const TileCoord& coord) const
    {
        auto it = bases.find(coord);
        return it != bases.end() ? &it->second : nullptr;
    }

    BaseHeightBlock& TerrainHeightLayerStore::adoptBase(
        const TileCoord& coord, const std::vector<float>& seed, uint32_t vertexCount)
    {
        auto it = bases.find(coord);
        if (it != bases.end())
            return it->second; // one-shot: never re-seed from an already-composited plane

        BaseHeightBlock block;
        block.heights = seed;
        block.vertexCount = vertexCount;
        block.dirty = false;
        return bases.emplace(coord, std::move(block)).first->second;
    }

    BaseHeightBlock& TerrainHeightLayerStore::adoptBase(
        const TileCoord& coord, std::vector<float>&& seed, uint32_t vertexCount)
    {
        auto it = bases.find(coord);
        if (it != bases.end())
            return it->second; // same one-shot rule as the copying overload

        BaseHeightBlock block;
        block.heights = std::move(seed);
        block.vertexCount = vertexCount;
        block.dirty = false;
        return bases.emplace(coord, std::move(block)).first->second;
    }

    void TerrainHeightLayerStore::eraseBase(const TileCoord& coord)
    {
        bases.erase(coord);
        derivedStale.erase(coord);
    }

    void TerrainHeightLayerStore::markBaseDirty(const TileCoord& coord)
    {
        auto it = bases.find(coord);
        if (it != bases.end())
            it->second.dirty = true;
    }

    bool TerrainHeightLayerStore::isBaseDirty(const TileCoord& coord) const
    {
        auto it = bases.find(coord);
        return it != bases.end() && it->second.dirty;
    }

    void TerrainHeightLayerStore::clearBaseDirty(const TileCoord& coord)
    {
        auto it = bases.find(coord);
        if (it != bases.end())
            it->second.dirty = false;
    }

    bool TerrainHeightLayerStore::addLayer(HeightLayerRecord record)
    {
        if (editingLocked)
            return false;

        stack.push_back(std::move(record));
        return true;
    }

    bool TerrainHeightLayerStore::removeLayer(uint64_t id)
    {
        auto it = std::find_if(stack.begin(), stack.end(),
                               [id](const HeightLayerRecord& r) { return r.id == id; });
        if (it == stack.end())
            return false;

        stack.erase(it);
        return true;
    }

    bool TerrainHeightLayerStore::setLayerVisible(uint64_t id, bool visible)
    {
        auto it = std::find_if(stack.begin(), stack.end(),
                               [id](const HeightLayerRecord& r) { return r.id == id; });
        if (it == stack.end())
            return false;

        it->visible = visible;
        return true;
    }

    bool TerrainHeightLayerStore::hasLayer(uint64_t id) const
    {
        return std::any_of(stack.begin(), stack.end(),
                           [id](const HeightLayerRecord& r) { return r.id == id; });
    }

    bool TerrainHeightLayerStore::isCovered(const TileCoord& coord) const
    {
        // Sticky, and deliberately so. Once a tile owns an authoritative base its derived plane
        // is regenerable output for the rest of the session, even after the last layer over it is
        // deleted -- with an empty stack compose simply yields `derived == base`, which is exactly
        // the right answer and needs no transition logic.
        //
        // The alternative, flipping back to "derived is authoritative" on the last removal, needs
        // a one-shot collapse at exactly that moment; miss it and the deleted spline's corridor
        // stays baked into the derived plane forever.
        //
        // Also ignores `visible`: hiding a layer must not reclassify the tile, or the base would
        // stop being authoritative mid-session and the next sculpt dab would land on derived.
        if (bases.find(coord) != bases.end())
            return true;

        return std::any_of(stack.begin(), stack.end(),
                           [&coord](const HeightLayerRecord& r)
                           { return r.affected.find(coord) != r.affected.end(); });
    }

    void TerrainHeightLayerStore::markDerivedStale(const TileCoord& coord)
    {
        derivedStale.insert(coord);
    }

    bool TerrainHeightLayerStore::isDerivedStale(const TileCoord& coord) const
    {
        return derivedStale.find(coord) != derivedStale.end();
    }

    void TerrainHeightLayerStore::clearDerivedStale(const TileCoord& coord)
    {
        derivedStale.erase(coord);
    }

    std::vector<TileCoord> TerrainHeightLayerStore::staleSorted() const
    {
        std::vector<TileCoord> result(derivedStale.begin(), derivedStale.end());
        std::sort(result.begin(), result.end(),
                  [](const TileCoord& a, const TileCoord& b)
                  { return a.z != b.z ? a.z < b.z : a.x < b.x; });
        return result;
    }

    HeightLayerTileEval makeSplineCorridorEval(SplineCorridorLayerParams params)
    {
        return [params = std::move(params)](
                   const TileCoord& coord, const TerrainTileConfig& config,
                   const std::vector<float>& in, std::vector<float>& out)
        {
            applySplineCorridorToTile(coord, config, params.samples, params.corridor, in, out);
        };
    }

    void composeTileHeights(
        const TileCoord& coord,
        const TerrainTileConfig& config,
        const std::vector<float>& base,
        const std::vector<HeightLayerRecord>& layers,
        std::vector<float>& out)
    {
        out = base;

        std::vector<float> in;
        for (const HeightLayerRecord& layer : layers)
        {
            if (!layer.visible || !layer.eval)
                continue;
            if (layer.affected.find(coord) == layer.affected.end())
                continue;

            // Reuses capacity after the first layer: the assignment is a memcpy, not a realloc,
            // once the sizes match.
            in = out;
            layer.eval(coord, config, in, out);
        }
    }
}
