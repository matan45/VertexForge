#pragma once
#include "EventTypes.hpp"
#include "../data/EntityHandle.hpp"
#include "../data/DTOs.hpp"
#include <optional>
#include <vector>
#include <string>

namespace events::material {

    // ============================================
    // COMMANDS - Operations that modify material state
    // ============================================

    // Add material component to an entity
    struct AddMaterialComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddMaterialComponent"; }
    };

    // Remove material component from an entity
    struct RemoveMaterialComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveMaterialComponent"; }
    };

    // Set material data on an entity
    struct SetMaterialDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::MaterialData materialData;

        std::string_view getName() const override { return "SetMaterialData"; }
    };

    // Set default material for an entity
    struct SetDefaultMaterialCommand : ICommand<bool> {
        services::EntityHandle entity;
        std::string materialPath;

        std::string_view getName() const override { return "SetDefaultMaterial"; }
    };

    // Set material for a specific submesh
    struct SetSubMeshMaterialCommand : ICommand<bool> {
        services::EntityHandle entity;
        std::string submeshName;
        std::string materialPath;

        std::string_view getName() const override { return "SetSubMeshMaterial"; }
    };

    // Set a runtime parameter override
    struct SetMaterialParameterCommand : ICommand<bool> {
        services::EntityHandle entity;
        std::string parameterName;
        float value;

        std::string_view getName() const override { return "SetMaterialParameter"; }
    };

    // Clear all submesh material assignments
    struct ClearSubMeshMaterialsCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "ClearSubMeshMaterials"; }
    };

    // ============================================
    // QUERIES - Read-only operations
    // ============================================

    // Check if entity has material component
    struct HasMaterialComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasMaterialComponent"; }
    };

    // Get material data from an entity
    struct GetMaterialDataQuery : IQuery<std::optional<services::MaterialData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetMaterialData"; }
    };

    // Get material path for a specific submesh
    struct GetSubMeshMaterialQuery : IQuery<std::string> {
        services::EntityHandle entity;
        std::string submeshName;

        std::string_view getName() const override { return "GetSubMeshMaterial"; }
    };

    // Get all submesh material assignments
    struct GetAllSubMeshMaterialsQuery : IQuery<std::map<std::string, std::string>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetAllSubMeshMaterials"; }
    };

    // ============================================
    // NOTIFICATIONS - State change broadcasts
    // ============================================

    // Material component added to entity
    struct MaterialComponentAddedNotification : INotification {
        services::EntityHandle entity;

        std::string_view getName() const override { return "MaterialComponentAdded"; }
    };

    // Material component removed from entity
    struct MaterialComponentRemovedNotification : INotification {
        services::EntityHandle entity;

        std::string_view getName() const override { return "MaterialComponentRemoved"; }
    };

    // Material data changed on entity
    struct MaterialDataChangedNotification : INotification {
        services::EntityHandle entity;
        std::string defaultMaterial;

        std::string_view getName() const override { return "MaterialDataChanged"; }
    };

    // Submesh material assignment changed
    struct SubMeshMaterialChangedNotification : INotification {
        services::EntityHandle entity;
        std::string submeshName;
        std::string materialPath;

        std::string_view getName() const override { return "SubMeshMaterialChanged"; }
    };

    // Material parameter override changed
    struct MaterialParameterChangedNotification : INotification {
        services::EntityHandle entity;
        std::string parameterName;
        float value;

        std::string_view getName() const override { return "MaterialParameterChanged"; }
    };

    // Material file saved to disk (for cache invalidation)
    struct MaterialFileSavedNotification : INotification {
        std::string materialPath;

        std::string_view getName() const override { return "MaterialFileSaved"; }
    };

}
