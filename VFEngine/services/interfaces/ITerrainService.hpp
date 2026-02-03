#pragma once
#include "../data/EntityHandle.hpp"
#include "../data/TerrainData.hpp"
#include <optional>

namespace services
{
    class ITerrainService
    {
    public:
        virtual ~ITerrainService() = default;

        virtual void registerEventHandlers() = 0;

        virtual EntityHandle createTerrain(const TerrainCreationData& config) = 0;
        virtual bool deleteTerrain(EntityHandle terrainEntity) = 0;
        virtual std::optional<TerrainData> getTerrainData(EntityHandle entity) const = 0;
        virtual bool hasTerrainComponent(EntityHandle entity) const = 0;
    };
}
