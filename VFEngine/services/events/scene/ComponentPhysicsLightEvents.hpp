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

}
