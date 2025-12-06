#pragma once
#include "EventTypes.hpp"
#include "../data/EntityHandle.hpp"
#include "../data/DTOs.hpp"
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

}
