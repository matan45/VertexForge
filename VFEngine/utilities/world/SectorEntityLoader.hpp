#pragma once

#include "WorldTypes.hpp"
#include <nlohmann/json.hpp>
#include <deque>
#include <vector>
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
    using EntityLoadedCallback = std::function<void(uint64_t uuid, const SectorCoord& coord)>;
    using EntityUnloadedCallback = std::function<void(uint64_t uuid, const SectorCoord& coord)>;
    using EntityPostLoadCallback = std::function<void(uint64_t uuid, const std::string& meshPath, const std::string& animatorPath)>;
    using EntityPreDestroyCallback = std::function<void(uint64_t entityHandleId)>;

    class SectorEntityLoader
    {
    private:
        struct PendingLoad
        {
            SectorCoord coord;
            std::string entityName;
            nlohmann::json entityJson;
        };

        struct PendingUnload
        {
            SectorCoord coord;
            uint64_t uuid;
        };

        std::deque<PendingLoad> pendingLoads;
        std::deque<PendingUnload> pendingUnloads;

        EntityLoadedCallback onEntityLoaded;
        EntityUnloadedCallback onEntityUnloaded;
        EntityPostLoadCallback onEntityPostLoad;
        EntityPreDestroyCallback onEntityPreDestroy;
    public:
        SectorEntityLoader() = default;

        void queueSectorLoadFromData(const SectorCoord& coord, std::vector<std::pair<std::string, nlohmann::json>>& entityNamesAndJson);
        void queueSectorUnload(const SectorCoord& coord, const std::vector<uint64_t>& uuids);

        void update(scene::SceneGraphSystem& sceneGraph, int maxEntitiesPerFrame = 8);

        void setOnEntityLoaded(EntityLoadedCallback callback) { onEntityLoaded = std::move(callback); }
        void setOnEntityUnloaded(EntityUnloadedCallback callback) { onEntityUnloaded = std::move(callback); }
        void setOnEntityPostLoad(EntityPostLoadCallback callback) { onEntityPostLoad = std::move(callback); }
        void setOnEntityPreDestroy(EntityPreDestroyCallback callback) { onEntityPreDestroy = std::move(callback); }

        void flush(scene::SceneGraphSystem& sceneGraph) { update(sceneGraph, std::numeric_limits<int>::max()); }

        void cancelPendingLoads(const SectorCoord& coord);

        void clear() { pendingLoads.clear(); pendingUnloads.clear(); }

        [[nodiscard]] bool hasPendingLoadsForSector(const SectorCoord& coord) const
        {
            for (const auto& load : pendingLoads)
            {
                if (load.coord == coord) return true;
            }
            return false;
        }
    };

} // namespace world
