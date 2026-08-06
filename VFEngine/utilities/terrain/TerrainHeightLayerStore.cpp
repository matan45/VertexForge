#include "TerrainHeightLayerStore.hpp"

#include "../print/Log.hpp"

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

    size_t TerrainHeightLayerStore::dropCoordFromLayers(const TileCoord& coord)
    {
        size_t dropped = 0;
        for (HeightLayerRecord& record : stack)
        {
            if (record.affected.erase(coord) != 0)
                ++dropped;
        }
        return dropped;
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

        // VK-1647. Both checks used to live only in the sidecar WRITER, which refuses the whole
        // file rather than one record -- so breaking either made the terrain silently unsaveable,
        // with the artist's stack still on screen. Refusing at the door keeps the in-RAM stack
        // always persistable.
        // 0 is the "no spline" sentinel: it is the default on RoadSplineComponent::splineId and on
        // every spline command that carries one, so a default-constructed
        // RemoveSplineHeightLayerCommand would address a real layer. reserveLayerId() and the
        // service counter both start at 1, so this is unreachable from the live paths — but a
        // sidecar can carry it, and the reader's uniqueness pass would not catch a single zero.
        if (record.id == 0)
        {
            vfLogError("TerrainHeightLayerStore: Refusing a layer with id 0 — that value is the "
                       "\"no spline\" sentinel");
            return false;
        }

        if (stack.size() >= MAX_TERRAIN_EDIT_LAYERS)
        {
            vfLogError("TerrainHeightLayerStore: Refusing layer {} — the stack already holds the "
                       "maximum of {} edit layers", record.id, MAX_TERRAIN_EDIT_LAYERS);
            return false;
        }

        // A duplicate is never benign: removeLayer, setLayerVisible and updateLayer all resolve by
        // first match, so a second record with the same id makes every later op address the older
        // one. This is exactly what a reload used to produce, before nextLayerId was persisted.
        if (hasLayer(record.id))
        {
            vfLogError("TerrainHeightLayerStore: Refusing a second layer with id {} — ids are "
                       "unique and never reused", record.id);
            return false;
        }

        // Keeps the watermark ahead of any id adopted from disk or handed in by a caller that
        // allocated elsewhere, so reserveLayerId() can never hand out a live id.
        adoptNextLayerId(record.id + 1);

        stack.push_back(std::move(record));
        return true;
    }

    bool TerrainHeightLayerStore::removeLayer(uint64_t id)
    {
        if (editingLocked)
            return false;

        auto it = std::find_if(stack.begin(), stack.end(),
                               [id](const HeightLayerRecord& r) { return r.id == id; });
        if (it == stack.end())
            return false;

        stack.erase(it);
        return true;
    }

    bool TerrainHeightLayerStore::setLayerVisible(uint64_t id, bool visible)
    {
        if (editingLocked)
            return false;

        auto it = std::find_if(stack.begin(), stack.end(),
                               [id](const HeightLayerRecord& r) { return r.id == id; });
        if (it == stack.end())
            return false;

        it->visible = visible;
        return true;
    }

    bool TerrainHeightLayerStore::setLayerName(uint64_t id, std::string name)
    {
        auto it = std::find_if(stack.begin(), stack.end(),
                               [id](const HeightLayerRecord& r) { return r.id == id; });
        if (it == stack.end())
            return false;

        it->name = std::move(name);
        return true;
    }

    bool TerrainHeightLayerStore::hasLayer(uint64_t id) const
    {
        return std::any_of(stack.begin(), stack.end(),
                           [id](const HeightLayerRecord& r) { return r.id == id; });
    }

    std::optional<size_t> TerrainHeightLayerStore::layerIndex(uint64_t id) const
    {
        auto it = std::find_if(stack.begin(), stack.end(),
                               [id](const HeightLayerRecord& r) { return r.id == id; });
        if (it == stack.end())
            return std::nullopt;

        return static_cast<size_t>(std::distance(stack.begin(), it));
    }

    const HeightLayerRecord* TerrainHeightLayerStore::layer(uint64_t id) const
    {
        auto it = std::find_if(stack.begin(), stack.end(),
                               [id](const HeightLayerRecord& r) { return r.id == id; });
        return it != stack.end() ? &*it : nullptr;
    }

    bool TerrainHeightLayerStore::moveLayer(uint64_t id, size_t newIndex)
    {
        if (editingLocked)
            return false;

        const std::optional<size_t> current = layerIndex(id);
        if (!current)
            return false;

        if (newIndex >= stack.size())
            return false;

        if (*current == newIndex)
            return true; // already there; a no-op is a success, not an error

        // std::rotate rather than std::swap. A swap would exchange the two records, moving the one
        // at `newIndex` all the way to `*current` -- so dragging one row past three others would
        // silently reorder two layers instead of one, and the composition result would not match
        // what the artist sees.
        auto first = stack.begin();
        if (*current < newIndex)
        {
            // Moving down: rotate [current, newIndex] left by one.
            std::rotate(first + static_cast<std::ptrdiff_t>(*current),
                        first + static_cast<std::ptrdiff_t>(*current) + 1,
                        first + static_cast<std::ptrdiff_t>(newIndex) + 1);
        }
        else
        {
            // Moving up: rotate [newIndex, current] right by one.
            std::rotate(first + static_cast<std::ptrdiff_t>(newIndex),
                        first + static_cast<std::ptrdiff_t>(*current),
                        first + static_cast<std::ptrdiff_t>(*current) + 1);
        }

        return true;
    }

    bool TerrainHeightLayerStore::updateLayer(uint64_t id, SplineCorridorLayerParams params,
                                              std::unordered_set<TileCoord, TileCoordHash> affected)
    {
        // Same gate as addLayer. Unreachable today -- the one caller returns early on the lock --
        // but the two mutators must not disagree about who enforces it, or the next caller
        // authored against the wrong one edits a stack whose bases could not be read.
        if (editingLocked)
            return false;

        auto it = std::find_if(stack.begin(), stack.end(),
                               [id](const HeightLayerRecord& r) { return r.id == id; });
        if (it == stack.end())
            return false;

        // In place, so the record keeps its id AND its stack position: re-authoring a road must not
        // move its corridor to the top of the stack, or every layer applied over it since would
        // start composing underneath it instead.
        it->type = HeightLayerType::SplineCorridor;
        it->spline = std::move(params);
        it->affected = std::move(affected);

        // Rebuilt from the stored parameters through the one construction site, never captured ad
        // hoc -- the same rule the sidecar loader follows, so an edited layer and a reloaded one are
        // the same callable by construction.
        it->eval = makeSplineCorridorEval(it->spline);

        return true;
    }

    std::unordered_set<TileCoord, TileCoordHash>
    TerrainHeightLayerStore::reorderImpactSet(uint64_t id, size_t lo, size_t hi) const
    {
        std::unordered_set<TileCoord, TileCoordHash> changed;

        const HeightLayerRecord* moved = layer(id);
        if (!moved || stack.empty() || lo >= stack.size())
            return changed;

        // A hidden layer contributes nothing at any tile, so moving it cannot change one. Skipping
        // is not merely an optimisation: layer ops recompose synchronously, so reordering a hidden
        // road that overlaps a long one would freeze for the length of both for no visual change --
        // and hide-then-reorder is the natural gesture in a list panel.
        //
        // Safe because a later show does not rely on this set: SetSplineHeightLayerVisibleCommand
        // routes through invalidateHeightLayerTiles, which marks the layer's ENTIRE affected set
        // plus rings stale -- a strict superset of anything a reorder could have unmasked.
        if (!moved->visible)
            return changed;

        hi = std::min(hi, stack.size() - 1);
        if (lo > hi)
            return changed;

        // Iterating the crossed layers and probing the moved layer's set, rather than the other way
        // round: a corridor's affected set is the length of one road, while the crossed range is
        // usually one or two layers.
        for (size_t i = lo; i <= hi; ++i)
        {
            if (stack[i].id == id || !stack[i].visible)
                continue;

            for (const TileCoord& coord : stack[i].affected)
            {
                if (moved->affected.find(coord) != moved->affected.end())
                    changed.insert(coord);
            }
        }

        return changed;
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
