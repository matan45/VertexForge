#include "WorldSectorManager.hpp"
#include "SectorAssignment.hpp"
#include "../print/Log.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace world
{
    namespace
    {
        // VK-1597: floorToSectorAxis moved to SectorAssignment.cpp so the pure, unit-testable
        // bucketing decision and this manager cannot drift apart. Behaviour is unchanged.

        // Sectors created from an engine-derived position can no longer land out of range (see
        // floorToSectorAxis), so this only fires for a hand-edited or corrupt .vfworld. The sector
        // is still created - dropping it would lose data - but its streaming id aliases another's.
        void logInvalidSectorCoordOnce(const SectorCoord& coord)
        {
            static std::atomic<bool> logged{false};
            if (logged.exchange(true, std::memory_order_relaxed))
                return;

            vfLogError("[WorldSector] Sector ({}, {}) is outside the addressable range [{}, {}]. "
                       "sectorCoordToId() packs each axis into 16 bits, so this sector aliases "
                       "another sector's streaming id. Further occurrences suppressed.",
                       coord.x, coord.z, kMinSectorCoord, kMaxSectorCoord);
        }
    }

    WorldSectorManager::WorldSectorManager(const SectorConfig& config)
        : config(config)
    {
    }

    void WorldSectorManager::setConfig(const SectorConfig& config)
    {
        this->config = config;
    }

    SectorCoord WorldSectorManager::worldPositionToSectorCoord(const glm::vec3& pos) const
    {
        return world::worldPositionToSectorCoord(pos, config);
    }

    SectorCoord WorldSectorManager::tileCoordToSectorCoord(const terrain::TileCoord& tileCoord, float worldTileSize) const
    {
        return world::tileOriginToSectorCoord(tileCoord.x, tileCoord.z, worldTileSize, config);
    }

    void WorldSectorManager::assignEntityToSector(uint64_t uuid, const glm::vec3& position)
    {
        assignEntityToSector(uuid, worldPositionToSectorCoord(position));
    }

    void WorldSectorManager::assignEntityToSector(uint64_t uuid, const SectorCoord& coord)
    {
        auto& sector = getOrCreateSector(coord);
        sector.entityUUIDs.push_back(uuid);
        sector.dirty = true;
        entityToSector[uuid] = coord;
    }

    void WorldSectorManager::removeEntityFromSector(uint64_t uuid, const SectorCoord& coord)
    {
        auto it = sectors.find(coord);
        if (it != sectors.end())
        {
            auto& uuids = it->second.entityUUIDs;
            uuids.erase(std::remove(uuids.begin(), uuids.end(), uuid), uuids.end());
            it->second.dirty = true;
        }
        entityToSector.erase(uuid);
    }

    void WorldSectorManager::reassignEntity(uint64_t uuid, const glm::vec3& oldPos, const glm::vec3& newPos)
    {
        SectorCoord oldCoord = worldPositionToSectorCoord(oldPos);
        SectorCoord newCoord = worldPositionToSectorCoord(newPos);

        if (oldCoord == newCoord)
            return;

        removeEntityFromSector(uuid, oldCoord);
        assignEntityToSector(uuid, newPos);
    }

    SectorCoord WorldSectorManager::getEntitySector(uint64_t uuid) const
    {
        auto it = entityToSector.find(uuid);
        if (it != entityToSector.end())
            return it->second;
        return SectorCoord(0, 0);
    }

    bool WorldSectorManager::hasEntitySector(uint64_t uuid) const
    {
        return entityToSector.contains(uuid);
    }

    WorldSector* WorldSectorManager::getSector(const SectorCoord& coord)
    {
        auto it = sectors.find(coord);
        return (it != sectors.end()) ? &it->second : nullptr;
    }

    const WorldSector* WorldSectorManager::getSector(const SectorCoord& coord) const
    {
        auto it = sectors.find(coord);
        return (it != sectors.end()) ? &it->second : nullptr;
    }

    WorldSector& WorldSectorManager::getOrCreateSector(const SectorCoord& coord)
    {
        auto it = sectors.find(coord);
        if (it != sectors.end())
            return it->second;

        if (!isValidSectorCoord(coord))
            logInvalidSectorCoordOnce(coord);

        auto& sector = sectors[coord];
        sector.coord = coord;
        return sector;
    }

    void WorldSectorManager::forEachSector(const std::function<void(WorldSector&)>& callback)
    {
        for (auto& [coord, sector] : sectors)
        {
            callback(sector);
        }
    }

    void WorldSectorManager::forEachSector(const std::function<void(const WorldSector&)>& callback) const
    {
        for (const auto& [coord, sector] : sectors)
        {
            callback(sector);
        }
    }

    std::vector<SectorCoord> WorldSectorManager::getLoadedSectors() const
    {
        std::vector<SectorCoord> result;
        for (const auto& [coord, sector] : sectors)
        {
            // VK-1591: deliberately Loaded ONLY — callers expect live entities, and a
            // Prefetched sector has none.
            if (sector.state == SectorState::Loaded)
                result.push_back(coord);
        }
        return result;
    }

    std::vector<SectorCoord> WorldSectorManager::getAllSectorCoords() const
    {
        std::vector<SectorCoord> result;
        result.reserve(sectors.size());
        for (const auto& [coord, sector] : sectors)
        {
            result.push_back(coord);
        }
        return result;
    }

    void WorldSectorManager::clear()
    {
        sectors.clear();
        entityToSector.clear();
    }

} // namespace world
