#pragma once

#include "events/EventTypes.hpp"
#include "events/terrain/SplineTerrainEvents.hpp"
#include "data/EntityHandle.hpp"
#include "asset/AssetRef.hpp"
#include "terrain/RoadMeshTypes.hpp"
#include "terrain/SplineTypes.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace windows
{
    // VK-1621 — turns an applied terrain spline into a road: a real .vfMesh asset plus the scene
    // entities that reference it.
    //
    // Lives Editor-side rather than in Services because writing a .vfMesh needs Import.dll, which
    // is Editor-only (premake5.lua:263 "Import is Editor-only"; Services links Terrain but not
    // Import, premake5.lua:521). It listens for SplineAppliedNotification, which the spline service
    // publishes AFTER the sculpt and paint ops, so the geometry is conformed to the deformed
    // terrain rather than the original ground.
    //
    // Not an ImguiWindow — it draws nothing. update() is called once a frame purely to give it a
    // lazy subscription point, matching how the tool panels self-register.
    class RoadMeshGenerator
    {
    public:
        RoadMeshGenerator() = default;
        ~RoadMeshGenerator();

        void update();

    public:
        // Everything needed to (re)create a road's entities from an already-written .vfMesh.
        // Split out so the undo command can respawn a road it removed without regenerating the
        // geometry — the asset is still on disk, only the entities went away.
        struct RoadSpawnDesc
        {
            std::string meshPath;
            terrain::SplineParams params;
            std::vector<glm::vec3> chunkOrigins;
            std::vector<glm::vec3> controlPoints;
            uint64_t splineId = 0;
            uint32_t revision = 0;
        };

        [[nodiscard]] static services::EntityHandle spawnRoad(const RoadSpawnDesc& desc);

        // A road that exists in the scene, for the tool panel's list.
        struct RoadEntry
        {
            services::EntityHandle entity;
            std::string name;
            uint32_t chunkCount = 0;
        };

        // Registry access is kept here rather than spread through the panels, matching how
        // MeshBrushServiceImpl owns its own instance bookkeeping.
        [[nodiscard]] static std::vector<RoadEntry> listRoads();

        // Seeds the Spline tool from a road's persisted component so it can be edited and
        // re-applied. Returns false when the entity is not a road.
        static bool openForEdit(services::EntityHandle entity);

    private:
        void subscribe();

        void generate(const events::splineTerrain::SplineAppliedNotification& applied);

        // Deletes a superseded road and its generated asset.
        void retireRoad(services::EntityHandle entity, const asset::AssetRef& meshRef) const;

        // Writes the chunks as one multi-submesh .vfMesh and registers it with the asset database.
        // Returns an empty string on failure.
        [[nodiscard]] std::string writeRoadAsset(const terrain::RoadMeshData& road,
                                                 const std::string& roadName,
                                                 uint32_t revision) const;

        events::SubscriptionToken appliedToken;
        bool subscribed = false;

        // Regenerating a road writes a NEW file rather than overwriting: ResourceManager caches
        // meshes by GUID and has no invalidateMeshCache (ResourceManager.hpp:39,89-92 — there are
        // material and terrain-material invalidators but no mesh one), and MeshStreamHandle holds
        // the file open (MeshStreamHandle.hpp:70). Overwriting in place would show stale geometry
        // at best and tear a live stream at worst.
        std::unordered_map<uint64_t, uint32_t> revisionBySpline;
    };
}
