#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/TerrainData.hpp"
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace events::terrain
{
    struct CreateTerrainCommand : ICommand<services::EntityHandle>
    {
        services::TerrainCreationData config;

        std::string_view getName() const override { return "CreateTerrain"; }
    };

    struct DeleteTerrainCommand : ICommand<bool>
    {
        services::EntityHandle terrainEntity;

        std::string_view getName() const override { return "DeleteTerrain"; }
    };

    struct GetTerrainDataQuery : IQuery<std::optional<services::TerrainData>>
    {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetTerrainData"; }
    };

    struct HasTerrainComponentQuery : IQuery<bool>
    {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasTerrainComponent"; }
    };

    struct HasTerrainTileComponentQuery : IQuery<bool>
    {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasTerrainTileComponent"; }
    };

    struct GetTerrainTileDataQuery : IQuery<std::optional<services::TerrainTileData>>
    {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetTerrainTileData"; }
    };

    struct RemapTerrainEntitiesCommand : ICommand<void>
    {
        std::string_view getName() const override { return "RemapTerrainEntities"; }
    };

    struct TerrainCreatedNotification : INotification
    {
        services::EntityHandle terrainEntity;
        services::TerrainCreationData config;

        std::string_view getName() const override { return "TerrainCreated"; }
    };

    struct TerrainDeletedNotification : INotification
    {
        services::EntityHandle terrainEntity;

        std::string_view getName() const override { return "TerrainDeleted"; }
    };

    struct SetTerrainMaterialPathCommand : ICommand<>
    {
        services::EntityHandle terrainEntity;
        std::string materialPath;

        std::string_view getName() const override { return "SetTerrainMaterialPath"; }
    };

    struct SaveWeightMapsCommand : ICommand<bool>
    {
        services::EntityHandle terrainEntity;
        std::string path;

        std::string_view getName() const override { return "SaveWeightMaps"; }
    };

    struct LoadWeightMapsCommand : ICommand<bool>
    {
        services::EntityHandle terrainEntity;
        std::string path;

        std::string_view getName() const override { return "LoadWeightMaps"; }
    };

    struct SaveTerrainCommand : ICommand<bool>
    {
        services::EntityHandle terrainEntity;
        std::string path;
        bool incremental = false;

        std::string_view getName() const override { return "SaveTerrain"; }
    };

    struct LoadTerrainCommand : ICommand<services::EntityHandle>
    {
        std::string path;

        std::string_view getName() const override { return "LoadTerrain"; }
    };

    struct SetTerrainSaveLockCommand : ICommand<>
    {
        bool locked = false;

        std::string_view getName() const override { return "SetTerrainSaveLock"; }
    };

    struct PrepareTerrainSaveCommand : ICommand<bool>
    {
        services::EntityHandle terrainEntity;
        bool incremental = false;

        std::string_view getName() const override { return "PrepareTerrainSave"; }
    };

    struct TerrainSavedNotification : INotification
    {
        services::EntityHandle terrainEntity;
        std::string path;

        std::string_view getName() const override { return "TerrainSaved"; }
    };

    struct SetTerrainColliderPropertiesCommand : ICommand<>
    {
        services::EntityHandle entity;
        uint8_t collisionLayer = 0;
        float friction = 0.5f;
        float restitution = 0.0f;

        std::string_view getName() const override { return "SetTerrainColliderProperties"; }
    };

    struct BeginTerrainLoadCommand : ICommand<bool>
    {
        std::string path;

        std::string_view getName() const override { return "BeginTerrainLoad"; }
    };

    struct PollTerrainLoadCommand : ICommand<std::optional<services::EntityHandle>>
    {
        std::string_view getName() const override { return "PollTerrainLoad"; }
    };

    struct TerrainLoadStartedNotification : INotification
    {
        std::string path;

        std::string_view getName() const override { return "TerrainLoadStarted"; }
    };

    struct TerrainLoadedNotification : INotification
    {
        services::EntityHandle terrainEntity;
        std::string path;

        std::string_view getName() const override { return "TerrainLoaded"; }
    };

    struct TerrainHeightfieldResult
    {
        float worldOriginX = 0.0f;
        float worldOriginZ = 0.0f;
        float tileWorldSize = 32.0f;
        float vertexSpacing = 1.0f;
        int32_t gridCountX = 0;
        int32_t gridCountZ = 0;
        uint32_t verticesPerTile = 33;
        std::vector<float> heights; // packed tile-by-tile, row-major (Z outer, X inner)
        bool valid = false;
    };

    struct GetTerrainHeightfieldQuery : IQuery<TerrainHeightfieldResult>
    {
        std::string_view getName() const override { return "GetTerrainHeightfield"; }
    };

    struct TerrainGeometryResult
    {
        std::vector<float> vertices; // Flat: x,y,z,x,y,z,...
        std::vector<int> triangles; // Index triplets
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
    };

    struct GetTerrainGeometryQuery : IQuery<TerrainGeometryResult>
    {
        std::string_view getName() const override { return "GetTerrainGeometry"; }
    };

    struct TerrainTileGeometryInfo
    {
        int32_t coordX = 0;
        int32_t coordZ = 0;
        glm::vec3 worldOrigin{0.0f};
        float tileSize = 32.0f;
        int firstVertexIndex = 0; // Vertex index (not float index)
        int vertexCount = 0;
        int firstTriangleIndex = 0; // Triangle index (not int index)
        int triangleCount = 0;
    };

    struct TerrainBakeGeometryResult
    {
        std::vector<float> vertices; // Flat: x,y,z,x,y,z,...
        std::vector<int> triangles; // Index triplets
        std::vector<TerrainTileGeometryInfo> tileInfos;
    };

    struct GetTerrainBakeGeometryQuery : IQuery<TerrainBakeGeometryResult>
    {
        std::string_view getName() const override { return "GetTerrainBakeGeometry"; }
    };

    struct TerrainMaterialCompiledNotification : INotification
    {
        std::string_view getName() const override { return "TerrainMaterialCompiled"; }
    };

    struct AddTerrainTileCommand : ICommand<bool>
    {
        services::EntityHandle terrainEntity;
        int32_t tileX = 0;
        int32_t tileZ = 0;

        std::string_view getName() const override { return "AddTerrainTile"; }
    };

    struct RemoveTerrainTileCommand : ICommand<bool>
    {
        services::EntityHandle terrainEntity;
        int32_t tileX = 0;
        int32_t tileZ = 0;

        std::string_view getName() const override { return "RemoveTerrainTile"; }
    };

    struct TerrainTileAddedNotification : INotification
    {
        services::EntityHandle terrainEntity;
        int32_t tileX = 0;
        int32_t tileZ = 0;

        std::string_view getName() const override { return "TerrainTileAdded"; }
    };

    struct TerrainTileRemovedNotification : INotification
    {
        services::EntityHandle terrainEntity;
        int32_t tileX = 0;
        int32_t tileZ = 0;

        std::string_view getName() const override { return "TerrainTileRemoved"; }
    };

    struct SetTerrainStreamingEnabledCommand : ICommand<>
    {
        services::EntityHandle terrainEntity;
        bool enabled = false;

        std::string_view getName() const override { return "SetTerrainStreamingEnabled"; }
    };

    struct StreamingConfigData
    {
        float loadRadius = 512.0f;
        float unloadRadius = 640.0f;
        int maxLoadsPerFrame = 4;
        int maxUnloadsPerFrame = 4;
    };

    struct SetTerrainStreamingConfigCommand : ICommand<>
    {
        services::EntityHandle terrainEntity;
        float loadRadius = 512.0f;
        float unloadRadius = 640.0f;
        int maxLoadsPerFrame = 4;
        int maxUnloadsPerFrame = 4;

        std::string_view getName() const override { return "SetTerrainStreamingConfig"; }
    };

    struct GetTerrainStreamingConfigQuery : IQuery<StreamingConfigData>
    {
        services::EntityHandle terrainEntity;

        std::string_view getName() const override { return "GetTerrainStreamingConfig"; }
    };

    struct IsTerrainStreamingEnabledQuery : IQuery<bool>
    {
        services::EntityHandle terrainEntity;

        std::string_view getName() const override { return "IsTerrainStreamingEnabled"; }
    };
}
