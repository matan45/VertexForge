#pragma once
#include "../math/RayBVH.hpp"
#include "../resource/Types.hpp"
#include <vector>
#include <string>
#include <optional>
#include <functional>

namespace lightbake
{
    // Progress callback: reports 0.0 to 1.0
    using BakeProgressCallback = std::function<void(float progress)>;

    // Per-tile terrain bake info
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

    // Flat terrain geometry data (world space), matching TerrainGeometryResult format
    struct TerrainBakeGeometry
    {
        std::vector<float> vertices;   // Flat: x,y,z,x,y,z,...
        std::vector<int> triangles;    // Index triplets
        std::vector<TerrainTileBakeInfo> tileInfos; // Per-tile metadata for lightmap baking
    };

    // Per-tile water geometry params
    struct WaterBakeTile
    {
        glm::vec3 worldOrigin{0.0f};
        float waterHeight = 0.0f;
        float worldTileSize = 32.0f;
        uint32_t subdivisions = 32;
    };

    // Loads static mesh data from disk, transforms to world space,
    // and builds a triangle-level BVH for ray intersection during light baking.
    // CPU-only — independent of GPU mesh cache.
    class BakeSceneMesh
    {
    public:
        BakeSceneMesh() = default;

        // Build the scene BVH from all static mesh entities in the ECS registry,
        // plus optional terrain and water geometry.
        // Loads .vfMesh files from disk, transforms vertices to world space,
        // and constructs a RayBVH from all triangles.
        // Returns true if at least one triangle was loaded.
        bool buildFromScene(
            const TerrainBakeGeometry& terrain = {},
            const std::vector<WaterBakeTile>& waterTiles = {},
            BakeProgressCallback progressCallback = nullptr);

        // Trace a ray and find the closest hit
        std::optional<math::RayHitResult> traceRay(
            const math::Ray& ray,
            float maxDist = std::numeric_limits<float>::max()) const;

        // Shadow/occlusion test — returns true if any geometry blocks the ray
        bool traceOcclusion(
            const math::Ray& ray,
            float maxDist = std::numeric_limits<float>::max()) const;

        // Release all CPU mesh data and BVH
        void clear();

        // Accessors
        bool isBuilt() const { return bvh_.isBuilt(); }
        size_t getTriangleCount() const { return bvh_.getTriangleCount(); }

        const math::RayBVH& getBVH() const { return bvh_; }

    private:
        void addTerrainTriangles(const TerrainBakeGeometry& terrain,
                                 std::vector<math::RayBVHTriangle>& triangles);
        void addWaterTriangles(const std::vector<WaterBakeTile>& waterTiles,
                               std::vector<math::RayBVHTriangle>& triangles);

        math::RayBVH bvh_;
    };
}
