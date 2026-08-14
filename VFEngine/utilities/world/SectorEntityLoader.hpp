#pragma once

#include "WorldExport.hpp"
#include "WorldTypes.hpp"
#include <nlohmann/json.hpp>
#include <deque>
#include <vector>
#include <unordered_set>
#include <cstdint>
#include <limits>
#include <functional>
#include <string>
#include <utility>
namespace scene
{
    class SceneGraphSystem;
}

namespace world
{
    // VK-1599: the grid travels with the coord. The loader is a SINGLE shared spawn queue across
    // every grid - it owns the frame's entity budget and the loadedThisFrame dedupe set, both of
    // which are whole-process resources - so a bare SectorCoord would be ambiguous the moment two
    // grids held a sector at the same coord, and the service would bucket the entity into the
    // wrong manager.
    using EntityLoadedCallback = std::function<void(uint64_t uuid, uint8_t gridIndex, const SectorCoord& coord)>;
    using EntityUnloadedCallback = std::function<void(uint64_t uuid, uint8_t gridIndex, const SectorCoord& coord)>;
    using EntityPostLoadCallback = std::function<void(uint64_t uuid, const std::string& meshPath, const std::string& animatorPath)>;
    using EntityPreDestroyCallback = std::function<void(uint64_t entityHandleId)>;

    // Aggregate sector-entity load progress, for loading-screen / streaming UI.
    // Counts are cumulative load items: queued counts every entity ever queued via
    // queueSectorLoadFromData; loaded counts every queued item consumed by update().
    struct SectorLoadProgress
    {
        uint64_t entitiesLoaded = 0;
        uint64_t entitiesQueued = 0;

        [[nodiscard]] uint64_t pending() const
        {
            return entitiesQueued > entitiesLoaded ? entitiesQueued - entitiesLoaded : 0;
        }
        // 1.0 when idle (nothing queued) or fully drained; otherwise loaded / queued.
        [[nodiscard]] float fraction() const
        {
            return entitiesQueued == 0
                       ? 1.0f
                       : static_cast<float>(entitiesLoaded) / static_cast<float>(entitiesQueued);
        }
    };

#pragma warning(push)
#pragma warning(disable: 4251)
    class VF_WORLD_API SectorEntityLoader
    {
    private:
        struct PendingLoad
        {
            uint8_t gridIndex = 0;
            SectorCoord coord;
            std::string entityName;
            nlohmann::json entityJson;
        };

        struct PendingUnload
        {
            uint8_t gridIndex = 0;
            SectorCoord coord;
            uint64_t uuid;
        };

        std::deque<PendingLoad> pendingLoads;
        std::deque<PendingUnload> pendingUnloads;
        std::unordered_set<uint64_t> loadedThisFrame;

        uint64_t totalQueuedLoads = 0;   // cumulative entities queued for load
        uint64_t totalProcessedLoads = 0; // cumulative queued entities consumed by update()

        EntityLoadedCallback onEntityLoaded;
        EntityUnloadedCallback onEntityUnloaded;
        EntityPostLoadCallback onEntityPostLoad;
        EntityPreDestroyCallback onEntityPreDestroy;
    public:
        SectorEntityLoader() = default;

        void queueSectorLoadFromData(uint8_t gridIndex, const SectorCoord& coord,
                                     std::vector<std::pair<std::string, nlohmann::json>>& entityNamesAndJson);
        void queueSectorUnload(uint8_t gridIndex, const SectorCoord& coord,
                               const std::vector<uint64_t>& uuids);

        void update(scene::SceneGraphSystem& sceneGraph, int maxEntitiesPerFrame = 8);

        void setOnEntityLoaded(EntityLoadedCallback callback) { onEntityLoaded = std::move(callback); }
        void setOnEntityUnloaded(EntityUnloadedCallback callback) { onEntityUnloaded = std::move(callback); }
        void setOnEntityPostLoad(EntityPostLoadCallback callback) { onEntityPostLoad = std::move(callback); }
        void setOnEntityPreDestroy(EntityPreDestroyCallback callback) { onEntityPreDestroy = std::move(callback); }

        void flush(scene::SceneGraphSystem& sceneGraph) { update(sceneGraph, std::numeric_limits<int>::max()); }

        void cancelPendingLoads(uint8_t gridIndex, const SectorCoord& coord);

        void clear()
        {
            pendingLoads.clear();
            pendingUnloads.clear();
            totalQueuedLoads = 0;
            totalProcessedLoads = 0;
        }

        [[nodiscard]] bool hasPendingLoadsForSector(uint8_t gridIndex, const SectorCoord& coord) const
        {
            for (const auto& load : pendingLoads)
            {
                if (load.gridIndex == gridIndex && load.coord == coord) return true;
            }
            return false;
        }

        [[nodiscard]] SectorLoadProgress getLoadProgress() const
        {
            return {totalProcessedLoads, totalQueuedLoads};
        }

        [[nodiscard]] size_t pendingLoadCount() const { return pendingLoads.size(); }
    };
#pragma warning(pop)

} // namespace world
