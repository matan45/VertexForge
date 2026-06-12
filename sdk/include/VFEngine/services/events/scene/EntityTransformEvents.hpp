#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <optional>
#include <vector>
#include <string>

namespace events::scene {

    // ============================================
    // Entity Lifecycle Commands
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

    struct DuplicateEntityCommand : ICommand<services::EntityHandle> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "DuplicateEntity"; }
    };

    // ============================================
    // Hierarchy Commands
    // ============================================

    struct ReparentEntityCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::EntityHandle newParent;

        std::string_view getName() const override { return "ReparentEntity"; }
    };

    struct ReorderEntityCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::EntityHandle newParent;
        int insertIndex = -1; // position among newParent's children; -1 = append

        std::string_view getName() const override { return "ReorderEntity"; }
    };

    // ============================================
    // Transform Commands
    // ============================================

    struct SetTransformCommand : ICommand<> {
        services::EntityHandle entity;
        services::TransformData transform;

        std::string_view getName() const override { return "SetTransform"; }
    };

    struct SetWorldTransformCommand : ICommand<> {
        services::EntityHandle entity;
        services::TransformData worldTransform;

        std::string_view getName() const override { return "SetWorldTransform"; }
    };

    // ============================================
    // Entity State Commands
    // ============================================

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

    struct SelectEntitiesCommand : ICommand<> {
        std::vector<services::EntityHandle> entities; // front() is the primary selection

        std::string_view getName() const override { return "SelectEntities"; }
    };

    struct SetEntityStaticCommand : ICommand<bool> {
        services::EntityHandle entity;
        bool isStatic;

        std::string_view getName() const override { return "SetEntityStatic"; }
    };

    // ============================================
    // Entity / Transform / Hierarchy Queries
    // ============================================

    struct GetEntityQuery : IQuery<std::optional<services::EntityData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetEntity"; }
    };

    struct GetTransformQuery : IQuery<std::optional<services::TransformData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetTransform"; }
    };

    struct GetWorldTransformQuery : IQuery<std::optional<services::TransformData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetWorldTransform"; }
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

    struct GetSelectedEntitiesQuery : IQuery<std::vector<services::EntityHandle>> {
        std::string_view getName() const override { return "GetSelectedEntities"; }
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

    struct IsEntityStaticQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "IsEntityStatic"; }
    };

    // ============================================
    // Entity / Transform / Hierarchy Notifications
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

    struct EntityStaticChangedNotification : INotification {
        services::EntityHandle entity;
        bool isStatic;

        std::string_view getName() const override { return "EntityStaticChanged"; }
    };

    struct EntityDuplicatedNotification : INotification {
        services::EntityHandle originalEntity;
        services::EntityHandle duplicatedEntity;

        std::string_view getName() const override { return "EntityDuplicated"; }
    };

}
