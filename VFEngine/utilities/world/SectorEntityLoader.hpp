#pragma once

#include "WorldTypes.hpp"
#include <deque>
#include <vector>
#include <cstdint>
#include <functional>
#include <string>

namespace scene
{
    class SceneGraphSystem;
}

namespace world
{
    using EntityLoadedCallback = std::function<void(uint64_t uuid, const SectorCoord& coord)>;
    using EntityUnloadedCallback = std::function<void(uint64_t uuid, const SectorCoord& coord)>;
    using EntityPostLoadCallback = std::function<void(uint64_t uuid, const std::string& meshPath, const std::string& animatorPath)>;
    using EntityPreUnloadCallback = std::function<void(uint64_t uuid, const std::string& meshPath)>;

    class SectorEntityLoader
    {
    public:
        SectorEntityLoader() = default;

        void queueSectorLoad(const SectorCoord& coord, const std::string& sectorFilePath);
        void queueSectorUnload(const SectorCoord& coord, const std::vector<uint64_t>& uuids);

        void update(scene::SceneGraphSystem& sceneGraph, int maxEntitiesPerFrame = 8);

        void setOnEntityLoaded(EntityLoadedCallback callback) { onEntityLoaded = std::move(callback); }
        void setOnEntityUnloaded(EntityUnloadedCallback callback) { onEntityUnloaded = std::move(callback); }
        void setOnEntityPostLoad(EntityPostLoadCallback callback) { onEntityPostLoad = std::move(callback); }
        void setOnEntityPreUnload(EntityPreUnloadCallback callback) { onEntityPreUnload = std::move(callback); }

        [[nodiscard]] bool hasPendingWork() const { return !pendingLoads.empty() || !pendingUnloads.empty(); }
        [[nodiscard]] size_t pendingLoadCount() const { return pendingLoads.size(); }
        [[nodiscard]] size_t pendingUnloadCount() const { return pendingUnloads.size(); }

    private:
        struct PendingLoad
        {
            SectorCoord coord;
            std::string entityName;
            std::string rawJson;
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
        EntityPreUnloadCallback onEntityPreUnload;
    };

} // namespace world
