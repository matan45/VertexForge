#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <cstddef>

namespace render::lighting
{
    // Cluster grid configuration constants
    // Memory budget: ~216KB total for default configuration
    //   - Params UBO: 128 bytes
    //   - AABBs SSBO (device): 16x9x24 x 32 bytes = ~108KB
    //   - AABBs staging: ~108KB
    namespace ClusterConstants
    {
        inline constexpr uint32_t DEFAULT_TILES_X = 16;    // Screen tiles horizontally
        inline constexpr uint32_t DEFAULT_TILES_Y = 9;     // Screen tiles vertically (16:9 aspect)
        inline constexpr uint32_t DEFAULT_SLICES_Z = 24;   // Depth slices (logarithmic)
        inline constexpr uint32_t MAX_CLUSTERS = 16 * 9 * 32; // 4608 max clusters
    }

    // GPU-aligned cluster grid parameters (UBO)
    // Must match GLSL layout in fragment/compute shaders
    struct alignas(16) GPUClusterGridParams
    {
        glm::uvec4 gridDimensions;     // 16 bytes - xyz = tilesX, tilesY, slicesZ, w = totalClusters
        glm::vec4 screenParams;        // 16 bytes - xy = screenSize, zw = tileSizePixels
        glm::vec4 depthParams;         // 16 bytes - x = near, y = far, z = log(far/near), w = 1/log(far/near)
        glm::mat4 invProjection;       // 64 bytes - for reconstructing view-space positions
        glm::vec4 clusterScale;        // 16 bytes - xyz = scale factors for cluster index calculation
        glm::vec4 clusterBias;         // 16 bytes - xyz = bias factors for cluster index calculation
    };
    static_assert(sizeof(GPUClusterGridParams) == 144, "GPUClusterGridParams must be 144 bytes");
    static_assert(offsetof(GPUClusterGridParams, gridDimensions) == 0, "GPUClusterGridParams::gridDimensions offset mismatch");
    static_assert(offsetof(GPUClusterGridParams, screenParams) == 16, "GPUClusterGridParams::screenParams offset mismatch");
    static_assert(offsetof(GPUClusterGridParams, depthParams) == 32, "GPUClusterGridParams::depthParams offset mismatch");
    static_assert(offsetof(GPUClusterGridParams, invProjection) == 48, "GPUClusterGridParams::invProjection offset mismatch");
    static_assert(offsetof(GPUClusterGridParams, clusterScale) == 112, "GPUClusterGridParams::clusterScale offset mismatch");
    static_assert(offsetof(GPUClusterGridParams, clusterBias) == 128, "GPUClusterGridParams::clusterBias offset mismatch");

    // GPU-aligned cluster AABB (view-space min/max)
    // View-space is used because lights are also culled in view-space
    struct alignas(16) GPUClusterAABB
    {
        glm::vec4 minPoint;    // 16 bytes - xyz = min corner (view-space), w = padding
        glm::vec4 maxPoint;    // 16 bytes - xyz = max corner (view-space), w = padding
    };
    static_assert(sizeof(GPUClusterAABB) == 32, "GPUClusterAABB must be 32 bytes");
    static_assert(offsetof(GPUClusterAABB, minPoint) == 0, "GPUClusterAABB::minPoint offset mismatch");
    static_assert(offsetof(GPUClusterAABB, maxPoint) == 16, "GPUClusterAABB::maxPoint offset mismatch");

    // CPU-side cluster grid configuration
    struct ClusterGridConfig
    {
        uint32_t tilesX = ClusterConstants::DEFAULT_TILES_X;
        uint32_t tilesY = ClusterConstants::DEFAULT_TILES_Y;
        uint32_t slicesZ = ClusterConstants::DEFAULT_SLICES_Z;

        [[nodiscard]] uint32_t getTotalClusters() const
        {
            return tilesX * tilesY * slicesZ;
        }
    };

    // Camera parameters that affect cluster computation
    // Clusters are rebuilt only when these parameters change
    struct ClusterCameraParams
    {
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        float fovY = 90.0f;              // Vertical FOV in degrees
        float aspectRatio = 16.0f / 9.0f;
        uint32_t screenWidth = 1920;
        uint32_t screenHeight = 1080;
        glm::mat4 projection{1.0f};
        glm::mat4 invProjection{1.0f};
    };
}
