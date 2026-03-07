#include "WorldSectorManager.hpp"
#include <cmath>

namespace world
{
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
        return SectorCoord(
            static_cast<int32_t>(std::floor(pos.x / config.sectorWorldSize)),
            static_cast<int32_t>(std::floor(pos.z / config.sectorWorldSize))
        );
    }

    SectorCoord WorldSectorManager::tileCoordToSectorCoord(const terrain::TileCoord& tileCoord, float worldTileSize) const
    {
        float worldX = static_cast<float>(tileCoord.x) * worldTileSize;
        float worldZ = static_cast<float>(tileCoord.z) * worldTileSize;
        return SectorCoord(
            static_cast<int32_t>(std::floor(worldX / config.sectorWorldSize)),
            static_cast<int32_t>(std::floor(worldZ / config.sectorWorldSize))
        );
    }

    void WorldSectorManager::assignEntityToSector(uint64_t uuid, const glm::vec3& position)
    {
        SectorCoord coord = worldPositionToSectorCoord(position);
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
