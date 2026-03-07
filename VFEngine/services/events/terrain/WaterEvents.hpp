#pragma once

#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/WaterData.hpp"
#include <glm/glm.hpp>
#include <optional>

namespace events::water
{
    // === Commands ===

    struct CreateWaterCommand : ICommand<services::EntityHandle> {
        services::WaterCreationData config;

        std::string_view getName() const override { return "CreateWater"; }
    };

    struct DeleteWaterCommand : ICommand<bool> {
        services::EntityHandle waterEntity;

        std::string_view getName() const override { return "DeleteWater"; }
    };

    struct SetWaterTileHeightCommand : ICommand<> {
        services::EntityHandle waterEntity;
        int32_t tileX = 0;
        int32_t tileZ = 0;
        float waterHeight = 0.0f;

        std::string_view getName() const override { return "SetWaterTileHeight"; }
    };

    struct AddWaterTileCommand : ICommand<bool> {
        services::EntityHandle waterEntity;
        int32_t tileX = 0;
        int32_t tileZ = 0;

        std::string_view getName() const override { return "AddWaterTile"; }
    };

    struct RemoveWaterTileCommand : ICommand<bool> {
        services::EntityHandle waterEntity;
        int32_t tileX = 0;
        int32_t tileZ = 0;

        std::string_view getName() const override { return "RemoveWaterTile"; }
    };

    struct SetWaterGlobalSettingsCommand : ICommand<> {
        services::EntityHandle waterEntity;
        services::WaterGlobalSettingsData settings;

        std::string_view getName() const override { return "SetWaterGlobalSettings"; }
    };

    // === Queries ===

    struct GetWaterDataQuery : IQuery<std::optional<services::WaterData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetWaterData"; }
    };

    struct IsPositionInWaterQuery : IQuery<bool> {
        glm::vec3 position{0.0f};

        std::string_view getName() const override { return "IsPositionInWater"; }
    };

    struct GetWaterHeightAtQuery : IQuery<float> {
        glm::vec2 worldXZ{0.0f};

        std::string_view getName() const override { return "GetWaterHeightAt"; }
    };

    struct HasWaterComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasWaterComponent"; }
    };

    struct HasWaterTileComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasWaterTileComponent"; }
    };

    struct GetWaterEntityQuery : IQuery<services::EntityHandle> {
        std::string_view getName() const override { return "GetWaterEntity"; }
    };

    struct GetWaterGlobalSettingsQuery : IQuery<services::WaterGlobalSettingsData> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetWaterGlobalSettings"; }
    };

    struct RebuildWaterFromComponentsCommand : ICommand<void> {
        std::string_view getName() const override { return "RebuildWaterFromComponents"; }
    };

    struct RemapWaterEntitiesCommand : ICommand<void> {
        std::string_view getName() const override { return "RemapWaterEntities"; }
    };

    struct SaveWaterCommand : ICommand<bool> {
        services::EntityHandle waterEntity;
        std::string path;

        std::string_view getName() const override { return "SaveWater"; }
    };

    struct LoadWaterCommand : ICommand<services::EntityHandle> {
        std::string path;

        std::string_view getName() const override { return "LoadWater"; }
    };

    // === Notifications ===

    struct WaterCreatedNotification : INotification {
        services::EntityHandle waterEntity;
        services::WaterCreationData config;

        std::string_view getName() const override { return "WaterCreated"; }
    };

    struct WaterDeletedNotification : INotification {
        services::EntityHandle waterEntity;

        std::string_view getName() const override { return "WaterDeleted"; }
    };

    struct WaterSavedNotification : INotification {
        services::EntityHandle waterEntity;
        std::string path;

        std::string_view getName() const override { return "WaterSaved"; }
    };

    struct WaterLoadedNotification : INotification {
        services::EntityHandle waterEntity;
        std::string path;

        std::string_view getName() const override { return "WaterLoaded"; }
    };

    struct WaterTileAddedNotification : INotification {
        services::EntityHandle waterEntity;
        int32_t tileX = 0;
        int32_t tileZ = 0;

        std::string_view getName() const override { return "WaterTileAdded"; }
    };

    struct WaterTileRemovedNotification : INotification {
        services::EntityHandle waterEntity;
        int32_t tileX = 0;
        int32_t tileZ = 0;

        std::string_view getName() const override { return "WaterTileRemoved"; }
    };

    struct WaterTileEnteredNotification : INotification {
        services::EntityHandle entity;
        int32_t tileX = 0;
        int32_t tileZ = 0;

        std::string_view getName() const override { return "WaterTileEntered"; }
    };

    struct WaterTileExitedNotification : INotification {
        services::EntityHandle entity;
        int32_t tileX = 0;
        int32_t tileZ = 0;

        std::string_view getName() const override { return "WaterTileExited"; }
    };
}
