#pragma once
#include "EventTypes.hpp"
#include "../data/EntityHandle.hpp"
#include "../data/TerrainData.hpp"
#include <optional>

namespace events::terrain
{
    // Command to create a new terrain with configuration
    struct CreateTerrainCommand : ICommand<services::EntityHandle> {
        services::TerrainCreationData config;

        std::string_view getName() const override { return "CreateTerrain"; }
    };

    // Command to delete terrain (removes parent and all tile children)
    struct DeleteTerrainCommand : ICommand<bool> {
        services::EntityHandle terrainEntity;

        std::string_view getName() const override { return "DeleteTerrain"; }
    };

    // Query to get terrain data from entity
    struct GetTerrainDataQuery : IQuery<std::optional<services::TerrainData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetTerrainData"; }
    };

    // Query to check if entity has terrain component
    struct HasTerrainComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasTerrainComponent"; }
    };

    // Notification when terrain is created
    struct TerrainCreatedNotification : INotification {
        services::EntityHandle terrainEntity;
        services::TerrainCreationData config;

        std::string_view getName() const override { return "TerrainCreated"; }
    };
}
