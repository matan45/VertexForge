#pragma once

#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/OceanData.hpp"
#include <optional>
#include <string>

namespace events::ocean
{
    // === Commands ===

    struct CreateOceanCommand : ICommand<services::EntityHandle> {
        services::OceanCreationData config;

        std::string_view getName() const override { return "CreateOcean"; }
    };

    struct DeleteOceanCommand : ICommand<bool> {
        services::EntityHandle oceanEntity;

        std::string_view getName() const override { return "DeleteOcean"; }
    };

    struct SetOceanVisualSettingsCommand : ICommand<void> {
        services::EntityHandle oceanEntity;
        services::OceanVisualSettings settings;

        std::string_view getName() const override { return "SetOceanVisualSettings"; }
    };

    struct SetOceanPhysicsSettingsCommand : ICommand<void> {
        services::EntityHandle oceanEntity;
        services::OceanPhysicsSettings settings;

        std::string_view getName() const override { return "SetOceanPhysicsSettings"; }
    };

    // === Ocean FFT Commands ===

    struct SetOceanFFTConfigCommand : ICommand<void> {
        services::OceanFFTConfigData config;

        std::string_view getName() const override { return "SetOceanFFTConfig"; }
    };

    // === Sea State Commands ===

    struct UpdateOceanCommand : ICommand<void> {
        float deltaTime = 0.0f;

        std::string_view getName() const override { return "UpdateOcean"; }
    };

    struct SetOceanSeaStateCommand : ICommand<void> {
        float beaufort = 3.0f;
        float transitionSeconds = 0.0f;

        std::string_view getName() const override { return "SetOceanSeaState"; }
    };

    struct SetOceanWeatherDrivenCommand : ICommand<void> {
        services::EntityHandle oceanEntity;
        bool enabled = false;
        float response = 1.0f;

        std::string_view getName() const override { return "SetOceanWeatherDriven"; }
    };

    struct GetOceanSeaStateQuery : IQuery<float> {
        std::string_view getName() const override { return "GetOceanSeaState"; }
    };

    // === Queries ===

    struct GetOceanEntityQuery : IQuery<services::EntityHandle> {
        std::string_view getName() const override { return "GetOceanEntity"; }
    };

    struct GetOceanDataQuery : IQuery<std::optional<services::OceanData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetOceanData"; }
    };

    struct HasOceanComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasOceanComponent"; }
    };

    struct GetOceanFFTConfigQuery : IQuery<services::OceanFFTConfigData> {
        std::string_view getName() const override { return "GetOceanFFTConfig"; }
    };

    struct IsOceanFFTEnabledQuery : IQuery<bool> {
        std::string_view getName() const override { return "IsOceanFFTEnabled"; }
    };

    struct GetOceanHeightAtQuery : IQuery<float> {
        glm::vec2 worldXZ{0.0f};

        std::string_view getName() const override { return "GetOceanHeightAt"; }
    };

    struct IsPositionInOceanQuery : IQuery<bool> {
        glm::vec3 position{0.0f};

        std::string_view getName() const override { return "IsPositionInOcean"; }
    };

    struct IsEntityInWaterQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "IsEntityInWater"; }
    };

    struct SaveOceanCommand : ICommand<bool> {
        services::EntityHandle oceanEntity;
        std::string path;

        std::string_view getName() const override { return "SaveOcean"; }
    };

    struct LoadOceanCommand : ICommand<services::EntityHandle> {
        std::string path;

        std::string_view getName() const override { return "LoadOcean"; }
    };

    struct RebuildOceanFromComponentsCommand : ICommand<void> {
        std::string_view getName() const override { return "RebuildOceanFromComponents"; }
    };

    // === Notifications ===

    struct OceanFFTConfigChangedNotification : INotification {
        services::OceanFFTConfigData config;

        std::string_view getName() const override { return "OceanFFTConfigChanged"; }
    };

    struct OceanCreatedNotification : INotification {
        services::EntityHandle oceanEntity;
        services::OceanCreationData config;

        std::string_view getName() const override { return "OceanCreated"; }
    };

    struct OceanDeletedNotification : INotification {
        services::EntityHandle oceanEntity;

        std::string_view getName() const override { return "OceanDeleted"; }
    };

    struct OceanSavedNotification : INotification {
        services::EntityHandle oceanEntity;
        std::string path;

        std::string_view getName() const override { return "OceanSaved"; }
    };

    struct OceanLoadedNotification : INotification {
        services::EntityHandle oceanEntity;
        std::string path;

        std::string_view getName() const override { return "OceanLoaded"; }
    };

    struct ObjectEnteredWaterNotification : INotification {
        services::EntityHandle entity;
        glm::vec3 position{0.0f};
        float verticalSpeed = 0.0f;
        float submersion = 0.0f;

        std::string_view getName() const override { return "ObjectEnteredWater"; }
    };

    struct ObjectExitedWaterNotification : INotification {
        services::EntityHandle entity;
        glm::vec3 position{0.0f};

        std::string_view getName() const override { return "ObjectExitedWater"; }
    };
}
