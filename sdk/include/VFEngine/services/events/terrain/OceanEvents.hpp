#pragma once

#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/OceanData.hpp"
#include "../../data/DTOs.hpp"
#include <optional>
#include <string>
#include <vector>

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

    // VK-1606: push one disturbance into the interactive ripple patch. Fire-and-forget from any
    // thread — the service queues it under a mutex and the render side drains the batch once per
    // frame. Positions outside the current patch are simply dropped by the sim.
    struct AddWaterImpulseCommand : ICommand<void> {
        glm::vec2 positionXZ{0.0f};
        float radius = 1.0f;        // metres; the kernel is exactly zero from here outwards
        float strength = 1.0f;      // vertical velocity kick at the centre (negative = downwards)

        std::string_view getName() const override { return "AddWaterImpulse"; }
    };

    struct SetWaterRippleEnabledCommand : ICommand<void> {
        bool enabled = false;

        std::string_view getName() const override { return "SetWaterRippleEnabled"; }
    };

    struct IsWaterRippleEnabledQuery : IQuery<bool> {
        std::string_view getName() const override { return "IsWaterRippleEnabled"; }
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

    // VK-1604: read the visual settings back in the exact struct SetOceanVisualSettingsCommand
    // takes, so a caller that only wants to change a few fields can read-modify-write instead of
    // sending a default-constructed struct and silently resetting everything else.
    struct GetOceanVisualSettingsQuery : IQuery<std::optional<services::OceanVisualSettings>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetOceanVisualSettings"; }
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

    // VK-1605: water depth (surface to sea floor) from the shore-depth field. Returns a very large
    // value where there is no terrain — "no bottom here" — rather than 0, so callers can treat it
    // as open ocean without a separate validity flag.
    struct GetWaterDepthAtQuery : IQuery<float> {
        glm::vec2 worldXZ{0.0f};

        std::string_view getName() const override { return "GetWaterDepthAt"; }
    };

    struct GetShoreDepthFieldStatusQuery : IQuery<services::ShoreDepthFieldStatus> {
        std::string_view getName() const override { return "GetShoreDepthFieldStatus"; }
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

    // === VK-1607: water bodies ===
    //
    // These live here, and are handled by OceanService, rather than following the
    // Add/Remove/Set/Get/Has convention of ComponentPhysicsLightEvents + PhysicsComponentService.
    // A water body is only meaningful against the ocean it coexists with - height resolution,
    // buoyancy and tile emission all have to consult both - so splitting ownership across two
    // services would mean the ocean asking another service what the water level is.

    // Creates a dedicated scene entity carrying a WaterBodyComponent, the way CreateOceanCommand
    // creates the ocean entity.
    struct CreateWaterBodyCommand : ICommand<services::EntityHandle> {
        services::WaterBodyComponentData data;
        glm::vec3 position{0.0f};
        std::string name = "WaterBody";

        std::string_view getName() const override { return "CreateWaterBody"; }
    };

    // Adds the component to an entity that already exists (the Add Component popup path).
    struct AddWaterBodyComponentCommand : ICommand<void> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddWaterBodyComponent"; }
    };

    struct RemoveWaterBodyComponentCommand : ICommand<void> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveWaterBodyComponent"; }
    };

    struct SetWaterBodyDataCommand : ICommand<void> {
        services::EntityHandle entity;
        services::WaterBodyComponentData data;

        std::string_view getName() const override { return "SetWaterBodyData"; }
    };

    struct GetWaterBodyDataQuery : IQuery<std::optional<services::WaterBodyComponentData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetWaterBodyData"; }
    };

    struct HasWaterBodyComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasWaterBodyComponent"; }
    };

    // Every active body in the scene, for the editor's list. The renderer does NOT go through this:
    // it pulls water::WaterBodyDesc straight off IOceanRenderProvider once per frame.
    struct GetWaterBodiesQuery : IQuery<std::vector<services::WaterBodyEntry>> {
        std::string_view getName() const override { return "GetWaterBodies"; }
    };

    // Is there ANY water in the scene - the ocean, or at least one active body. A cheap once-per-tick
    // gate for callers that would otherwise pay a per-entity query for nothing.
    struct HasAnyWaterQuery : IQuery<bool> {
        std::string_view getName() const override { return "HasAnyWater"; }
    };

    // The water surface at a point, or nullopt when there is none there. GetOceanHeightAtQuery
    // cannot answer this: it returns 0 both for "no water" and for "the water really is at y = 0",
    // which was harmless while the only water was an infinite ocean and is not any more - a lake
    // covers part of the world and dry land covers the rest.
    struct GetWaterSurfaceAtQuery : IQuery<std::optional<float>> {
        glm::vec2 worldXZ{0.0f};
        // A water body has a floor, so the answer depends on how deep the query is: below the floor
        // there is no body here at all and the ocean (if any) takes over. Leave at 0 for callers
        // that genuinely only have a column - they get the body whenever y = 0 is inside it.
        float worldY = 0.0f;

        std::string_view getName() const override { return "GetWaterSurfaceAt"; }
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
