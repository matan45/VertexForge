#pragma once

#include "WorldTypes.hpp"
#include "WorldSector.hpp"
#include "../terrain/TerrainTypes.hpp"
#include <glm/glm.hpp>
#include <unordered_map>
#include <functional>

namespace world
{
    class WorldSectorManager
    {
    public:
        explicit WorldSectorManager(const SectorConfig& config = {});

        void setConfig(const SectorConfig& config);
        [[nodiscard]] const SectorConfig& getConfig() const { return config; }

        [[nodiscard]] SectorCoord worldPositionToSectorCoord(const glm::vec3& pos) const;
        [[nodiscard]] SectorCoord tileCoordToSectorCoord(const terrain::TileCoord& tileCoord, float worldTileSize) const;

        void assignEntityToSector(uint64_t uuid, const glm::vec3& position);
        void removeEntityFromSector(uint64_t uuid, const SectorCoord& coord);
        void reassignEntity(uint64_t uuid, const glm::vec3& oldPos, const glm::vec3& newPos);

        [[nodiscard]] SectorCoord getEntitySector(uint64_t uuid) const;
        [[nodiscard]] bool hasEntitySector(uint64_t uuid) const;

        [[nodiscard]] WorldSector* getSector(const SectorCoord& coord);
        [[nodiscard]] const WorldSector* getSector(const SectorCoord& coord) const;
        [[nodiscard]] WorldSector& getOrCreateSector(const SectorCoord& coord);

        void forEachSector(const std::function<void(WorldSector&)>& callback);
        void forEachSector(const std::function<void(const WorldSector&)>& callback) const;

        [[nodiscard]] std::vector<SectorCoord> getLoadedSectors() const;
        [[nodiscard]] std::vector<SectorCoord> getAllSectorCoords() const;

        void clear();

    private:
        SectorConfig config;
        std::unordered_map<SectorCoord, WorldSector, SectorCoordHash> sectors;
        std::unordered_map<uint64_t, SectorCoord> entityToSector;
    };

} // namespace world
