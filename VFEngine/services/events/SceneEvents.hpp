#pragma once
#include "EventTypes.hpp"
#include "../data/EntityHandle.hpp"
#include "../data/DTOs.hpp"
#include "types/PhysicsTypes.hpp"
#include "types/AudioTypes.hpp"
#include <optional>
#include <vector>
#include <string>

namespace events::scene {

    struct CreateEntityCommand : ICommand<services::EntityHandle> {
        std::string name;
        std::optional<services::EntityHandle> parent;

        std::string_view getName() const override { return "CreateEntity"; }
    };

    struct DeleteEntityCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "DeleteEntity"; }
    };

    struct ReparentEntityCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::EntityHandle newParent;

        std::string_view getName() const override { return "ReparentEntity"; }
    };

    struct SetTransformCommand : ICommand<> {
        services::EntityHandle entity;
        services::TransformData transform;

        std::string_view getName() const override { return "SetTransform"; }
    };

    struct SetEntityNameCommand : ICommand<> {
        services::EntityHandle entity;
        std::string newName;

        std::string_view getName() const override { return "SetEntityName"; }
    };

    struct SetEntityActiveCommand : ICommand<> {
        services::EntityHandle entity;
        bool isActive;

        std::string_view getName() const override { return "SetEntityActive"; }
    };

    struct SelectEntityCommand : ICommand<> {
        std::optional<services::EntityHandle> entity;

        std::string_view getName() const override { return "SelectEntity"; }
    };

    struct SetIBLDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::IBLData iblData;

        std::string_view getName() const override { return "SetIBLData"; }
    };

    struct RemoveIBLComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveIBLComponent"; }
    };

    struct AddCameraComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddCameraComponent"; }
    };

    struct RemoveCameraComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveCameraComponent"; }
    };

    struct SetCameraDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::CameraData cameraData;

        std::string_view getName() const override { return "SetCameraData"; }
    };
    
    struct AddMeshComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddMeshComponent"; }
    };

    struct RemoveMeshComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveMeshComponent"; }
    };

    struct SetMeshDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::MeshData meshData;

        std::string_view getName() const override { return "SetMeshData"; }
    };

    struct SetEntityStaticCommand : ICommand<bool> {
        services::EntityHandle entity;
        bool isStatic;

        std::string_view getName() const override { return "SetEntityStatic"; }
    };

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

    struct NewSceneCommand : ICommand<bool> {
        std::string_view getName() const override { return "NewScene"; }
    };

    struct SaveSceneCommand : ICommand<bool> {
        std::string filePath;

        std::string_view getName() const override { return "SaveScene"; }
    };

    struct LoadSceneCommand : ICommand<bool> {
        std::string filePath;

        std::string_view getName() const override { return "LoadScene"; }
    };

    struct SavePrefabCommand : ICommand<bool> {
        services::EntityHandle entity;
        std::string filePath;

        std::string_view getName() const override { return "SavePrefab"; }
    };

    struct LoadPrefabCommand : ICommand<std::optional<services::EntityHandle>> {
        std::string filePath;
        std::optional<services::EntityHandle> parent;  // nullopt = add to scene root

        std::string_view getName() const override { return "LoadPrefab"; }
    };

    struct DuplicateEntityCommand : ICommand<services::EntityHandle> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "DuplicateEntity"; }
    };

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

    struct AddVFXComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddVFXComponent"; }
    };

    struct RemoveVFXComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveVFXComponent"; }
    };

    struct SetVFXDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::VFXData vfxData;

        std::string_view getName() const override { return "SetVFXData"; }
    };

    struct SetPhysicsSettingsCommand : ICommand<bool> {
        types::PhysicsSettings settings;

        std::string_view getName() const override { return "SetPhysicsSettings"; }
    };

    struct GetEntityQuery : IQuery<std::optional<services::EntityData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetEntity"; }
    };

    struct GetTransformQuery : IQuery<std::optional<services::TransformData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetTransform"; }
    };

    struct GetSceneHierarchyQuery : IQuery<services::SceneHierarchyData> {
        std::string_view getName() const override { return "GetSceneHierarchy"; }
    };

    struct GetEntitiesWithComponentQuery : IQuery<std::vector<services::EntityHandle>> {
        services::ComponentTypeId componentType;

        std::string_view getName() const override { return "GetEntitiesWithComponent"; }
    };

    struct GetSelectedEntityQuery : IQuery<std::optional<services::EntityHandle>> {
        std::string_view getName() const override { return "GetSelectedEntity"; }
    };

    struct FindEntitiesByNameQuery : IQuery<std::vector<services::EntityHandle>> {
        std::string name;

        std::string_view getName() const override { return "FindEntitiesByName"; }
    };

    struct GetPrimaryCameraQuery : IQuery<std::optional<services::EntityHandle>> {
        std::string_view getName() const override { return "GetPrimaryCamera"; }
    };

    struct GetRootEntityQuery : IQuery<services::EntityHandle> {
        std::string_view getName() const override { return "GetRootEntity"; }
    };

    struct GetCameraDataQuery : IQuery<std::optional<services::CameraData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetCameraData"; }
    };

    struct HasCameraComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasCameraComponent"; }
    };

    struct HasIBLComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasIBLComponent"; }
    };

    struct GetIBLDataQuery : IQuery<std::optional<services::IBLData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetIBLData"; }
    };
    
    struct HasMeshComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasMeshComponent"; }
    };

    struct GetMeshDataQuery : IQuery<std::optional<services::MeshData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetMeshData"; }
    };

    struct IsEntityStaticQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "IsEntityStatic"; }
    };

    struct HasAudioSource2DComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasAudioSource2DComponent"; }
    };

    struct GetAudioSource2DDataQuery : IQuery<std::optional<services::AudioSource2DData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetAudioSource2DData"; }
    };

    struct HasAudioSource3DComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasAudioSource3DComponent"; }
    };

    struct GetAudioSource3DDataQuery : IQuery<std::optional<services::AudioSource3DData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetAudioSource3DData"; }
    };

    struct HasColliderComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasColliderComponent"; }
    };

    struct GetColliderDataQuery : IQuery<std::optional<services::ColliderComponentData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetColliderData"; }
    };

    struct HasRigidBodyComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasRigidBodyComponent"; }
    };

    struct GetRigidBodyDataQuery : IQuery<std::optional<services::RigidBodyComponentData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetRigidBodyData"; }
    };

    struct HasVFXComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasVFXComponent"; }
    };

    struct GetVFXDataQuery : IQuery<std::optional<services::VFXData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetVFXData"; }
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

    struct EntityCreatedNotification : INotification {
        services::EntityHandle entity;
        std::string name;
        std::optional<services::EntityHandle> parent;

        std::string_view getName() const override { return "EntityCreated"; }
    };

    struct EntityDeletedNotification : INotification {
        services::EntityHandle entity;

        std::string_view getName() const override { return "EntityDeleted"; }
    };

    struct EntityReparentedNotification : INotification {
        services::EntityHandle entity;
        std::optional<services::EntityHandle> oldParent;
        services::EntityHandle newParent;

        std::string_view getName() const override { return "EntityReparented"; }
    };

    struct TransformChangedNotification : INotification {
        services::EntityHandle entity;
        services::TransformData newTransform;

        std::string_view getName() const override { return "TransformChanged"; }
    };

    struct EntitySelectedNotification : INotification {
        std::optional<services::EntityHandle> entity;

        std::string_view getName() const override { return "EntitySelected"; }
    };

    struct SceneLoadedNotification : INotification {
        std::string scenePath;

        std::string_view getName() const override { return "SceneLoaded"; }
    };

    struct SceneClearedNotification : INotification {
        std::string_view getName() const override { return "SceneCleared"; }
    };
    
    struct SceneLoadingStartedNotification : INotification {
        std::string scenePath;

        std::string_view getName() const override { return "SceneLoadingStarted"; }
    };

    struct SceneLoadingProgressUpdatedNotification : INotification {
        std::string currentEntityName;
        float progress;  // 0.0 - 1.0

        std::string_view getName() const override { return "SceneLoadingProgressUpdated"; }
    };

    struct SceneLoadingCompletedNotification : INotification {
        std::string scenePath;
        bool success;
        std::string errorMessage;  // Only set if success is false

        std::string_view getName() const override { return "SceneLoadingCompleted"; }
    };

    struct MeshDataChangedNotification : INotification {
        services::EntityHandle entity;
        std::string meshPath;
        std::string animatorPath;

        std::string_view getName() const override { return "MeshDataChanged"; }
    };

    struct EntityStaticChangedNotification : INotification {
        services::EntityHandle entity;
        bool isStatic;

        std::string_view getName() const override { return "EntityStaticChanged"; }
    };

    struct PrefabCreatedNotification : INotification {
        std::string filePath;
        services::EntityHandle sourceEntity;

        std::string_view getName() const override { return "PrefabCreated"; }
    };

    struct PrefabInstantiatedNotification : INotification {
        std::string filePath;
        services::EntityHandle rootEntity;

        std::string_view getName() const override { return "PrefabInstantiated"; }
    };

    struct EntityDuplicatedNotification : INotification {
        services::EntityHandle originalEntity;
        services::EntityHandle duplicatedEntity;

        std::string_view getName() const override { return "EntityDuplicated"; }
    };

}
