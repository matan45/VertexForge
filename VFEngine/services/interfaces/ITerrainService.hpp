#pragma once
#include "../data/EntityHandle.hpp"
#include "../data/TerrainData.hpp"
#include <optional>

namespace services
{
    // Terrain service interface - manages terrain entity hierarchy
    class ITerrainService
    {
    public:
        virtual ~ITerrainService() = default;

        // Register CQRS event handlers
        virtual void registerEventHandlers() = 0;

        // Create a new terrain with the given configuration
        // Returns the parent terrain entity handle
        virtual EntityHandle createTerrain(const TerrainCreationData& config) = 0;

        // Delete a terrain and all its tile children
        virtual bool deleteTerrain(EntityHandle terrainEntity) = 0;

        // Get terrain data from an entity
        virtual std::optional<TerrainData> getTerrainData(EntityHandle entity) const = 0;

        // Check if an entity has a terrain component
        virtual bool hasTerrainComponent(EntityHandle entity) const = 0;
    };
}
