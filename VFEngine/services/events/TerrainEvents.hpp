#pragma once
#include "EventTypes.hpp"
#include "../data/EntityHandle.hpp"
#include "../data/TerrainData.hpp"
#include <optional>

namespace events::terrain
{
    struct CreateTerrainCommand : ICommand<services::EntityHandle> {
        services::TerrainCreationData config;

        std::string_view getName() const override { return "CreateTerrain"; }
    };

    struct DeleteTerrainCommand : ICommand<bool> {
        services::EntityHandle terrainEntity;

        std::string_view getName() const override { return "DeleteTerrain"; }
    };

    struct GetTerrainDataQuery : IQuery<std::optional<services::TerrainData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetTerrainData"; }
    };

    struct HasTerrainComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasTerrainComponent"; }
    };

    struct HasTerrainTileComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasTerrainTileComponent"; }
    };

    struct GetTerrainTileDataQuery : IQuery<std::optional<services::TerrainTileData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetTerrainTileData"; }
    };

    struct TerrainCreatedNotification : INotification {
        services::EntityHandle terrainEntity;
        services::TerrainCreationData config;

        std::string_view getName() const override { return "TerrainCreated"; }
    };

    struct TerrainDeletedNotification : INotification {
        services::EntityHandle terrainEntity;

        std::string_view getName() const override { return "TerrainDeleted"; }
    };
}
