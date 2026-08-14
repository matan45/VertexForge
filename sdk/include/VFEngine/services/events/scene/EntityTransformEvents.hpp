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

    // VK-1433 Phase 4 — tag/untag an entity as the Prefab Rig Preview editing sandbox root.
    // Mirrors MarkUIPreviewSandboxCommand: tagging adds PreviewSandboxTagComponent AND marks the
    // root inactive (so the scene serializer skips it and the main passes ignore the subtree);
    // untagging removes the tag and restores isActive=true. The offscreen prefab-rig preview reads
    // the entities directly, so the inactive flag never affects what the window renders.
    struct MarkPreviewSandboxCommand : ICommand<bool> {
        services::EntityHandle entity;
        bool tagged = true;

        std::string_view getName() const override { return "MarkPreviewSandbox"; }
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

    // VK-1597: UE5's "Is Spatially Loaded". false pins the entity out of World Sector streaming.
    // Owned by the scene layer (EntityStateService), not the world service, so the inspector
    // checkbox still works with no world open; WorldSectorServiceImpl reacts to the notification.
    struct SetEntitySpatiallyLoadedCommand : ICommand<bool> {
        services::EntityHandle entity;
        bool spatiallyLoaded;

        std::string_view getName() const override { return "SetEntitySpatiallyLoaded"; }
    };

    // VK-1599: which named runtime grid the entity streams on. Same ownership split as the
    // spatially-loaded flag above - the scene layer owns the component, the world service reacts.
    struct SetEntityStreamingGridCommand : ICommand<bool> {
        services::EntityHandle entity;
        uint8_t gridIndex = 0;

        std::string_view getName() const override { return "SetEntityStreamingGrid"; }
    };

    struct GetEntityStreamingGridQuery : IQuery<uint8_t> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetEntityStreamingGrid"; }
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

    // VK-1597: true when StreamingPolicyComponent is absent - absence IS the default.
    struct IsEntitySpatiallyLoadedQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "IsEntitySpatiallyLoaded"; }
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

    // VK-1597: published only when the value actually changed. A notification rather than a
    // world-service command on purpose - publish is fire-and-forget, so the scene layer stays
    // usable in a build or a session where no world service is registered.
    struct EntityStreamingPolicyChangedNotification : INotification {
        services::EntityHandle entity;
        bool spatiallyLoaded;
        // VK-1599: the grid the entity now names. The world service re-buckets against BOTH fields
        // - a grid change is a migration between two managers, so it has to unbucket from wherever
        // the entity currently sits before assigning on the grid it now belongs to.
        uint8_t gridIndex = 0;

        std::string_view getName() const override { return "EntityStreamingPolicyChanged"; }
    };

    struct EntityDuplicatedNotification : INotification {
        services::EntityHandle originalEntity;
        services::EntityHandle duplicatedEntity;

        std::string_view getName() const override { return "EntityDuplicated"; }
    };

}
