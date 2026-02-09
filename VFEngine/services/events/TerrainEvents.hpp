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

    struct RemapTerrainEntitiesCommand : ICommand<void> {
        std::string_view getName() const override { return "RemapTerrainEntities"; }
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

    struct SetTerrainMaterialPathCommand : ICommand<> {
        services::EntityHandle terrainEntity;
        std::string materialPath;

        std::string_view getName() const override { return "SetTerrainMaterialPath"; }
    };

    struct SaveWeightMapsCommand : ICommand<bool> {
        services::EntityHandle terrainEntity;
        std::string path;

        std::string_view getName() const override { return "SaveWeightMaps"; }
    };

    struct LoadWeightMapsCommand : ICommand<bool> {
        services::EntityHandle terrainEntity;
        std::string path;

        std::string_view getName() const override { return "LoadWeightMaps"; }
    };

    struct SaveTerrainCommand : ICommand<bool> {
        services::EntityHandle terrainEntity;
        std::string path;

        std::string_view getName() const override { return "SaveTerrain"; }
    };

    struct LoadTerrainCommand : ICommand<services::EntityHandle> {
        std::string path;

        std::string_view getName() const override { return "LoadTerrain"; }
    };

    struct SetTerrainSaveLockCommand : ICommand<> {
        bool locked = false;

        std::string_view getName() const override { return "SetTerrainSaveLock"; }
    };

    struct TerrainSavedNotification : INotification {
        services::EntityHandle terrainEntity;
        std::string path;

        std::string_view getName() const override { return "TerrainSaved"; }
    };

    struct BeginTerrainLoadCommand : ICommand<bool> {
        std::string path;

        std::string_view getName() const override { return "BeginTerrainLoad"; }
    };

    struct PollTerrainLoadCommand : ICommand<std::optional<services::EntityHandle>> {
        std::string_view getName() const override { return "PollTerrainLoad"; }
    };

    struct TerrainLoadStartedNotification : INotification {
        std::string path;

        std::string_view getName() const override { return "TerrainLoadStarted"; }
    };

    struct TerrainLoadedNotification : INotification {
        services::EntityHandle terrainEntity;
        std::string path;

        std::string_view getName() const override { return "TerrainLoaded"; }
    };
}
