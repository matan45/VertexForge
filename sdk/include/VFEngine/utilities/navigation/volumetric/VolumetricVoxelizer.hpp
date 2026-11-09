#pragma once
#include "VoxelGrid.hpp"
#include "../../threading/CancellationToken.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace volumetric
{
    struct VoxelSphere
    {
        glm::vec3 center{0.0f};
        float radius = 0.0f;
    };

    // A segment a-b swept by a sphere.
    struct VoxelCapsule
    {
        glm::vec3 a{0.0f};
        glm::vec3 b{0.0f};
        float radius = 0.0f;
    };

    // A world-space snapshot of the static scene inside one volume (VK-1660), taken on the main thread so the
    // bake itself touches no engine state.
    struct VolumetricBakeInput
    {
        glm::vec3 boundsMin{0.0f}; // the volume's world box
        glm::vec3 boundsMax{0.0f};
        VolumetricBakeSettings settings;
        // Triangles of meshes, box / convex / mesh colliders and foliage: the voxels they touch are blocked, and
        // so is free space they close off from the volume's boundary (a sealed room).
        std::vector<float> vertices;   // x, y, z, ...
        std::vector<uint32_t> indices; // triangle triplets
        // Terrain triangles: the voxels they touch are blocked, and so is every voxel below the terrain surface
        // in its column.
        std::vector<float> terrainVertices;
        std::vector<uint32_t> terrainIndices;
        std::vector<VoxelSphere> spheres;   // solid
        std::vector<VoxelCapsule> capsules; // solid
    };

    struct VolumetricBakeResult
    {
        std::shared_ptr<const VoxelGrid> grid; // null when the bake failed or was cancelled
        std::string error;
        bool cancelled = false;
        int64_t voxels = 0;
        int64_t navigable = 0;
    };

    // The grid dimensions a box gets: ceil(extent / voxelSize) per axis; zero on an empty box or a voxel size
    // <= 0.
    glm::ivec3 gridDimensionsFor(const glm::vec3& boundsMin, const glm::vec3& boundsMax, float voxelSize);
    // Their product (int64); 0 when gridDimensionsFor is zero.
    int64_t voxelCountFor(const glm::vec3& boundsMin, const glm::vec3& boundsMax, float voxelSize);
    // The smallest voxel size whose grid for the box has at most kMaxVoxels voxels.
    float minVoxelSizeFor(const glm::vec3& boundsMin, const glm::vec3& boundsMax);

    // Bakes the grid, origin = boundsMin:
    //  1. blocked = every voxel a triangle (triangle-box overlap, SAT), sphere or capsule touches, plus every voxel
    //     below the terrain surface in each column;
    //  2. a flood fill from the free voxels on the grid's boundary (6-connected) reaches the open space; free voxels
    //     it does not reach are enclosed and become blocked;
    //  3. blocked voxels grow by ceil(clearance / voxelSize) voxels (separable distance, per axis), so navigable
    //     voxels keep the clearance from geometry. The grid's outer faces are not walls.
    // The result's grid is navigable where the agent fits. Runs on the calling thread (it may fan out over the
    // JobSystem; inline without one), polls `cancel` between stages / slabs and reports progress 0..1. Errors (no
    // grid): an empty or non-finite box, voxelSize or clearance out of range, connectivity not 6 or 26, more than
    // kMaxVoxels voxels ("... raise voxelSize to >= X").
    VolumetricBakeResult voxelize(const VolumetricBakeInput& input, const threading::CancellationToken* cancel = nullptr,
                                  const std::function<void(float)>& progress = {});
}
