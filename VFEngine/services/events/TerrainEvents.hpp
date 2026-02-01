#pragma once
#include "EventTypes.hpp"
#include "../data/EntityHandle.hpp"
#include "../data/TerrainData.hpp"
#include "math/Frustum.hpp"
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace events::terrain
{
    // Simple tile info returned by terrain service
    // Graphics layer converts this to MeshRenderData
    struct TerrainTileInfo
    {
        int32_t coordX = 0;
        int32_t coordZ = 0;
        uint8_t currentLOD = 0;
        glm::vec3 worldOrigin{0.0f};
        glm::vec3 aabbMin{0.0f};
        glm::vec3 aabbMax{0.0f};
    };
    // Command to create a new terrain with configuration
    struct CreateTerrainCommand : ICommand<services::EntityHandle> {
        services::TerrainCreationData config;

        std::string_view getName() const override { return "CreateTerrain"; }
    };

    // Command to delete terrain (removes parent and all tile children)
    struct DeleteTerrainCommand : ICommand<bool> {
        services::EntityHandle terrainEntity;

        std::string_view getName() const override { return "DeleteTerrain"; }
    };

    // Query to get terrain data from entity
    struct GetTerrainDataQuery : IQuery<std::optional<services::TerrainData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetTerrainData"; }
    };

    // Query to check if entity has terrain component
    struct HasTerrainComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasTerrainComponent"; }
    };

    // Notification when terrain is created
    struct TerrainCreatedNotification : INotification {
        services::EntityHandle terrainEntity;
        services::TerrainCreationData config;

        std::string_view getName() const override { return "TerrainCreated"; }
    };

    // Notification when terrain is deleted
    struct TerrainDeletedNotification : INotification {
        services::EntityHandle terrainEntity;

        std::string_view getName() const override { return "TerrainDeleted"; }
    };

    // Query to get visible terrain tiles info
    // Graphics layer converts this to MeshRenderData
    struct GetVisibleTerrainTilesQuery : IQuery<std::vector<TerrainTileInfo>> {
        math::Frustum frustum;
        glm::vec3 cameraPosition{0.0f};

        std::string_view getName() const override { return "GetVisibleTerrainTiles"; }
    };

    // Command to upload terrain tiles to GPU buffers
    struct UploadTerrainTilesCommand : ICommand<bool> {
        services::EntityHandle terrainEntity;

        std::string_view getName() const override { return "UploadTerrainTiles"; }
    };

    // Command to sync terrain LODs based on camera position
    struct UpdateTerrainLODsCommand : ICommand<void> {
        glm::vec3 cameraPosition{0.0f};

        std::string_view getName() const override { return "UpdateTerrainLODs"; }
    };
}
