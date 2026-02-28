#pragma once
#include "../math/RayBVH.hpp"
#include "../resource/Types.hpp"
#include <vector>
#include <string>
#include <optional>
#include <functional>

namespace lightbake
{
    using BakeProgressCallback = std::function<void(float progress)>;

    struct TerrainTileBakeInfo
    {
        int32_t coordX = 0;
        int32_t coordZ = 0;
        glm::vec3 worldOrigin{0.0f};
        float tileSize = 32.0f;
        int firstVertexIndex = 0;  // Index into flat vertices array (vertex index, not float index)
        int vertexCount = 0;
        int firstTriangleIndex = 0; // Index into flat triangles array (triangle index, not int index)
        int triangleCount = 0;
    };

    // Synthetic entity ID base for terrain tiles (high bit set to avoid collision with real entities)
    constexpr uint32_t TERRAIN_ENTITY_BASE = 0x80000000u;

    struct TerrainBakeGeometry
    {
        std::vector<float> vertices;   // Flat: x,y,z,x,y,z,...
        std::vector<int> triangles;    // Index triplets
        std::vector<TerrainTileBakeInfo> tileInfos; // Per-tile metadata for lightmap baking
    };

    struct WaterBakeTile
    {
        glm::vec3 worldOrigin{0.0f};
        float waterHeight = 0.0f;
        float worldTileSize = 32.0f;
        uint32_t subdivisions = 32;
    };

    class BakeSceneMesh
    {
    public:
        BakeSceneMesh() = default;

        bool buildFromScene(
            const TerrainBakeGeometry& terrain = {},
            const std::vector<WaterBakeTile>& waterTiles = {},
            BakeProgressCallback progressCallback = nullptr);

        std::optional<math::RayHitResult> traceRay(
            const math::Ray& ray,
            float maxDist = std::numeric_limits<float>::max()) const;

        bool traceOcclusion(
            const math::Ray& ray,
            float maxDist = std::numeric_limits<float>::max()) const;

        void clear();

        bool isBuilt() const { return bvh_.isBuilt(); }
        size_t getTriangleCount() const { return bvh_.getTriangleCount(); }

        const math::RayBVH& getBVH() const { return bvh_; }

    private:
        struct MeshEntity
        {
            std::string meshPath;
            glm::mat4 worldMatrix;
            glm::mat3 normalMatrix;
            uint32_t entityId;
        };

        std::vector<MeshEntity> collectStaticMeshEntities();
        void loadMeshTriangles(const std::vector<MeshEntity>& meshEntities,
                               std::vector<math::RayBVHTriangle>& triangles,
                               BakeProgressCallback progressCallback);
        void addTerrainTriangles(const TerrainBakeGeometry& terrain,
                                 std::vector<math::RayBVHTriangle>& triangles);
        void addWaterTriangles(const std::vector<WaterBakeTile>& waterTiles,
                               std::vector<math::RayBVHTriangle>& triangles);

        math::RayBVH bvh_;
    };
}
