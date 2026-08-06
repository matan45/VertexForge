#pragma once
#include "EntityHandle.hpp"
#include "terrain/HeightmapLoader.hpp"
#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <optional>

namespace services
{
    struct TerrainCreationData
    {
        int32_t tilesX = 4;
        int32_t tilesZ = 4;

        uint8_t resolution = 0;
        float worldTileSize = 32.0f;

        float maxHeight = 100.0f;
        float minHeight = -10.0f;

        std::string heightmapPath;
        std::vector<terrain::HeightmapRegion> heightmapRegions;
        std::string terrainMaterialPath;
        std::string weightMapPath;
    };

    struct TerrainCreationPollResult
    {
        bool inProgress = false;
        float progress = 0.0f;
        std::string stage;
        std::optional<EntityHandle> result;
    };

    // VK-1647. One reserved height layer, as the Editor may see it. Deliberately POD and
    // deliberately NOT terrain::HeightLayerRecord: that type carries a std::function, an
    // unordered_set of TileCoord and a polyline, and the Editor does not link Terrain.dll
    // (premake5.lua) — the same reason SplineTerrainUndoEvents.hpp is split out of
    // SplineTerrainEvents.hpp.
    struct HeightLayerInfo
    {
        uint64_t id = 0;
        uint32_t order = 0; // position in composition order; 0 composes first
        bool visible = true;
        uint32_t affectedTileCount = 0;
    };

    // What a wide layer invalidation still owes. Polled per frame, in the shape the navmesh and
    // volumetric bakes already use (status + fraction + counts).
    struct HeightLayerRecomposeProgress
    {
        // Covered, stale, and resident: the tiles the per-frame recompose budget will actually get
        // to. This is the number that drains.
        uint32_t pendingResident = 0;

        // Covered, stale, and streamed out. Waiting on the streamer, not on the recompose budget,
        // so it is reported separately: folded into the fraction it would pin a bar below 100% for
        // as long as the camera stays away, which reads as a hang rather than as "not loaded".
        uint32_t pendingUnloaded = 0;

        // The existing 8-tiles-per-frame geometry queue. A recompose only marks a tile dirty; the
        // mesh the user sees is rebuilt afterwards, so this is usually the longer half.
        uint32_t meshBacklog = 0;

        // High-water mark of (pendingResident + meshBacklog) since the last time both hit zero.
        // Latched by the service so the fraction is monotone even though new work can arrive
        // mid-drain.
        uint32_t totalAtStart = 0;

        float progress = 1.0f; // 1.0 when idle
        bool active = false;
    };

    struct TerrainData
    {
        uint8_t resolution = 0;
        float worldTileSize = 0.0f;
        float maxHeight = 0.0f;
        float minHeight = 0.0f;
        int32_t gridMinX = 0;
        int32_t gridMinZ = 0;
        int32_t gridMaxX = 0;
        int32_t gridMaxZ = 0;
        std::string heightmapPath;
        std::vector<terrain::HeightmapRegion> heightmapRegions;
        std::string terrainMaterialPath;
        std::string weightMapPath;
        uint32_t tileCount = 0;

        bool isActive = true;
        bool isDirty = false;
        uint32_t activeTileCount = 0;
        uint32_t visibleTileCount = 0;

        std::string savePath;
        bool saveDirty = false;

        // Collider properties (from TerrainColliderComponent)
        uint8_t colliderCollisionLayer = 0;
        float colliderFriction = 0.5f;
        float colliderRestitution = 0.0f;
    };

    struct TerrainTileData
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;
        bool isVisible = true;
        bool isDirty = false;
        bool isGPUResident = false;
        float boundingMinY = 0.0f;
        float boundingMaxY = 0.0f;
    };
}
