#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include "types/PhysicsTypes.hpp"
#include "types/AudioTypes.hpp"
#include "types/RenderSettings.hpp"
#include <optional>

namespace events::scene {

    // ============================================
    // Audio Source 2D Component Events
    // ============================================

    struct AddAudioSource2DComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddAudioSource2DComponent"; }
    };

    struct RemoveAudioSource2DComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveAudioSource2DComponent"; }
    };

    struct SetAudioSource2DDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::AudioSource2DData audioData;

        std::string_view getName() const override { return "SetAudioSource2DData"; }
    };

    struct HasAudioSource2DComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasAudioSource2DComponent"; }
    };

    struct GetAudioSource2DDataQuery : IQuery<std::optional<services::AudioSource2DData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetAudioSource2DData"; }
    };

    // ============================================
    // Audio Source 3D Component Events
    // ============================================

    struct AddAudioSource3DComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddAudioSource3DComponent"; }
    };

    struct RemoveAudioSource3DComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveAudioSource3DComponent"; }
    };

    struct SetAudioSource3DDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::AudioSource3DData audioData;

        std::string_view getName() const override { return "SetAudioSource3DData"; }
    };

    // Narrow authoring path for viewport attenuation handles. Unlike the full
    // DTO setter, this never retargets clips or creates a missing component.
    struct SetAudioSource3DDistancesCommand : ICommand<bool> {
        services::EntityHandle entity;
        float minDistance = 0.0f;
        float maxDistance = 0.0f;

        std::string_view getName() const override { return "SetAudioSource3DDistances"; }
    };

    struct HasAudioSource3DComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasAudioSource3DComponent"; }
    };

    struct GetAudioSource3DDataQuery : IQuery<std::optional<services::AudioSource3DData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetAudioSource3DData"; }
    };

    // ============================================
    // Collider Component Events
    // ============================================

    struct AddColliderComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddColliderComponent"; }
    };

    struct RemoveColliderComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveColliderComponent"; }
    };

    struct SetColliderDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::ColliderComponentData colliderData;

        std::string_view getName() const override { return "SetColliderData"; }
    };

    struct HasColliderComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasColliderComponent"; }
    };

    struct GetColliderDataQuery : IQuery<std::optional<services::ColliderComponentData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetColliderData"; }
    };

    // ============================================
    // RigidBody Component Events
    // ============================================

    struct AddRigidBodyComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddRigidBodyComponent"; }
    };

    struct RemoveRigidBodyComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveRigidBodyComponent"; }
    };

    struct SetRigidBodyDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::RigidBodyComponentData rigidBodyData;

        std::string_view getName() const override { return "SetRigidBodyData"; }
    };

    struct HasRigidBodyComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasRigidBodyComponent"; }
    };

    struct GetRigidBodyDataQuery : IQuery<std::optional<services::RigidBodyComponentData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetRigidBodyData"; }
    };

    // ============================================
    // Vehicle Component Events
    // ============================================

    struct AddVehicleComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddVehicleComponent"; }
    };

    struct RemoveVehicleComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveVehicleComponent"; }
    };

    struct SetVehicleDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::VehicleComponentData vehicleData;

        std::string_view getName() const override { return "SetVehicleData"; }
    };

    struct HasVehicleComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasVehicleComponent"; }
    };

    struct GetVehicleDataQuery : IQuery<std::optional<services::VehicleComponentData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetVehicleData"; }
    };

    // ============================================
    // Buoyancy Component Events
    // ============================================

    struct AddBuoyancyComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddBuoyancyComponent"; }
    };

    struct RemoveBuoyancyComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveBuoyancyComponent"; }
    };

    struct SetBuoyancyDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::BuoyancyComponentData buoyancyData;

        std::string_view getName() const override { return "SetBuoyancyData"; }
    };

    struct HasBuoyancyComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasBuoyancyComponent"; }
    };

    struct GetBuoyancyDataQuery : IQuery<std::optional<services::BuoyancyComponentData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetBuoyancyData"; }
    };

    // ============================================
    // Water Wake Emitter Component Events (VK-1606)
    // ============================================

    struct AddWaterWakeEmitterComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddWaterWakeEmitterComponent"; }
    };

    struct RemoveWaterWakeEmitterComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveWaterWakeEmitterComponent"; }
    };

    struct SetWaterWakeEmitterDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::WaterWakeEmitterComponentData emitterData;

        std::string_view getName() const override { return "SetWaterWakeEmitterData"; }
    };

    struct HasWaterWakeEmitterComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasWaterWakeEmitterComponent"; }
    };

    struct GetWaterWakeEmitterDataQuery : IQuery<std::optional<services::WaterWakeEmitterComponentData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetWaterWakeEmitterData"; }
    };

    // ============================================
    // Physics Animation Component Events
    // ============================================

    struct AddPhysicsAnimationComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddPhysicsAnimationComponent"; }
    };

    struct RemovePhysicsAnimationComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemovePhysicsAnimationComponent"; }
    };

    struct SetPhysicsAnimationDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::PhysicsAnimationComponentData data;

        std::string_view getName() const override { return "SetPhysicsAnimationData"; }
    };

    struct HasPhysicsAnimationComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasPhysicsAnimationComponent"; }
    };

    struct GetPhysicsAnimationDataQuery : IQuery<std::optional<services::PhysicsAnimationComponentData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetPhysicsAnimationData"; }
    };

    // ============================================
    // Navmesh Agent Component Events
    // ============================================

    struct AddNavmeshAgentComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddNavmeshAgentComponent"; }
    };

    struct RemoveNavmeshAgentComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveNavmeshAgentComponent"; }
    };

    // ============================================
    // Off-Mesh Link Component Events
    // ============================================

    struct AddOffMeshLinkComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddOffMeshLinkComponent"; }
    };

    struct RemoveOffMeshLinkComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveOffMeshLinkComponent"; }
    };

    struct HasOffMeshLinkComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasOffMeshLinkComponent"; }
    };

    // ============================================
    // Navmesh Obstacle Component Events
    // ============================================

    struct AddNavmeshObstacleComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddNavmeshObstacleComponent"; }
    };

    struct RemoveNavmeshObstacleComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveNavmeshObstacleComponent"; }
    };

    struct HasNavmeshObstacleComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasNavmeshObstacleComponent"; }
    };

    // ============================================
    // Navmesh Modifier Volume Component Events
    // ============================================

    struct AddNavmeshModifierVolumeComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddNavmeshModifierVolumeComponent"; }
    };

    struct RemoveNavmeshModifierVolumeComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveNavmeshModifierVolumeComponent"; }
    };

    struct HasNavmeshModifierVolumeComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasNavmeshModifierVolumeComponent"; }
    };

    // ============================================
    // Nav Invoker Component Events
    // ============================================

    struct AddNavInvokerComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddNavInvokerComponent"; }
    };

    struct RemoveNavInvokerComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveNavInvokerComponent"; }
    };

    struct HasNavInvokerComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasNavInvokerComponent"; }
    };

    // ============================================
    // Volumetric Nav Volume Component Events
    // ============================================

    struct AddVolumetricNavVolumeComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddVolumetricNavVolumeComponent"; }
    };

    struct RemoveVolumetricNavVolumeComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveVolumetricNavVolumeComponent"; }
    };

    struct HasVolumetricNavVolumeComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasVolumetricNavVolumeComponent"; }
    };

    // ============================================
    // Volumetric Agent Component Events
    // ============================================

    struct AddVolumetricAgentComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddVolumetricAgentComponent"; }
    };

    struct RemoveVolumetricAgentComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveVolumetricAgentComponent"; }
    };

    struct HasVolumetricAgentComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasVolumetricAgentComponent"; }
    };

    // ============================================
    // Controller Component Events
    // ============================================

    struct AddControllerComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddControllerComponent"; }
    };

    struct RemoveControllerComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveControllerComponent"; }
    };

    // ============================================
    // Behavior Tree Component Events
    // ============================================

    struct AddBehaviorTreeComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddBehaviorTreeComponent"; }
    };

    struct RemoveBehaviorTreeComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveBehaviorTreeComponent"; }
    };

    // ============================================
    // Light Component Events (Directional)
    // ============================================

    struct AddDirectionalLightComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddDirectionalLightComponent"; }
    };

    struct RemoveDirectionalLightComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveDirectionalLightComponent"; }
    };

    struct SetDirectionalLightDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::DirectionalLightData lightData;

        std::string_view getName() const override { return "SetDirectionalLightData"; }
    };

    struct HasDirectionalLightComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasDirectionalLightComponent"; }
    };

    struct GetDirectionalLightDataQuery : IQuery<std::optional<services::DirectionalLightData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetDirectionalLightData"; }
    };

    // ============================================
    // Light Component Events (Point)
    // ============================================

    struct AddPointLightComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddPointLightComponent"; }
    };

    struct RemovePointLightComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemovePointLightComponent"; }
    };

    struct SetPointLightDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::PointLightData lightData;

        std::string_view getName() const override { return "SetPointLightData"; }
    };

    struct HasPointLightComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasPointLightComponent"; }
    };

    struct GetPointLightDataQuery : IQuery<std::optional<services::PointLightData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetPointLightData"; }
    };

    // ============================================
    // Light Component Events (Spot)
    // ============================================

    struct AddSpotLightComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddSpotLightComponent"; }
    };

    struct RemoveSpotLightComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveSpotLightComponent"; }
    };

    struct SetSpotLightDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::SpotLightData lightData;

        std::string_view getName() const override { return "SetSpotLightData"; }
    };

    struct HasSpotLightComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasSpotLightComponent"; }
    };

    struct GetSpotLightDataQuery : IQuery<std::optional<services::SpotLightData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetSpotLightData"; }
    };

    // ============================================
    // Shadow Override Component Events
    // ============================================

    struct HasShadowOverrideQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasShadowOverride"; }
    };

    struct GetShadowOverrideDataQuery : IQuery<std::optional<services::ShadowOverrideData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetShadowOverrideData"; }
    };

    struct SetShadowOverrideDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::ShadowOverrideData data;

        std::string_view getName() const override { return "SetShadowOverrideData"; }
    };

    struct RemoveShadowOverrideCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveShadowOverride"; }
    };

    // ============================================
    // Settings Commands / Queries
    // ============================================

    struct SetPhysicsSettingsCommand : ICommand<bool> {
        types::PhysicsSettings settings;

        std::string_view getName() const override { return "SetPhysicsSettings"; }
    };

    struct GetPhysicsSettingsQuery : IQuery<types::PhysicsSettings> {
        std::string_view getName() const override { return "GetPhysicsSettings"; }
    };

    struct SetAudioSettingsCommand : ICommand<bool> {
        types::AudioSettings settings;

        std::string_view getName() const override { return "SetAudioSettings"; }
    };

    struct GetAudioSettingsQuery : IQuery<types::AudioSettings> {
        std::string_view getName() const override { return "GetAudioSettings"; }
    };

    struct SetRenderSettingsCommand : ICommand<bool> {
        types::RenderSettings settings;

        std::string_view getName() const override { return "SetRenderSettings"; }
    };

    struct GetRenderSettingsQuery : IQuery<types::RenderSettings> {
        std::string_view getName() const override { return "GetRenderSettings"; }
    };

    // ============================================
    // Destructible Component Events
    // ============================================

    struct AddDestructibleComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddDestructibleComponent"; }
    };

    struct RemoveDestructibleComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveDestructibleComponent"; }
    };

    struct SetDestructibleDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::DestructibleComponentData data;

        std::string_view getName() const override { return "SetDestructibleData"; }
    };

    struct HasDestructibleComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasDestructibleComponent"; }
    };

    struct GetDestructibleDataQuery : IQuery<std::optional<services::DestructibleComponentData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetDestructibleData"; }
    };

}
