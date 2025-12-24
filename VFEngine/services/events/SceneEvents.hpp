#pragma once
#include "EventTypes.hpp"
#include "../data/EntityHandle.hpp"
#include "../data/DTOs.hpp"
#include "../interfaces/IAudioService.hpp"
#include <optional>
#include <vector>
#include <string>

namespace events::scene {

    // ============================================
    // COMMANDS - Operations that modify scene state
    // ============================================

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

    // Audio Source Component Commands
    struct AddAudioSourceComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddAudioSourceComponent"; }
    };

    struct RemoveAudioSourceComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveAudioSourceComponent"; }
    };

    struct SetAudioSourceDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::AudioSourceData audioData;

        std::string_view getName() const override { return "SetAudioSourceData"; }
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

    // ============================================
    // QUERIES - Read-only operations
    // ============================================

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

    // Audio Source Component Queries
    struct HasAudioSourceComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasAudioSourceComponent"; }
    };

    struct GetAudioSourceDataQuery : IQuery<std::optional<services::AudioSourceData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetAudioSourceData"; }
    };

    // ============================================
    // NOTIFICATIONS - State change broadcasts
    // ============================================

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

        std::string_view getName() const override { return "MeshDataChanged"; }
    };

    struct EntityStaticChangedNotification : INotification {
        services::EntityHandle entity;
        bool isStatic;

        std::string_view getName() const override { return "EntityStaticChanged"; }
    };

}
