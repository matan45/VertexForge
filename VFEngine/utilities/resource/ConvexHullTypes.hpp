#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace resource
{
    // Single convex hull from V-HACD decomposition
    struct ConvexHull
    {
        std::vector<glm::vec3> vertices;  // Hull vertices
        std::vector<uint32_t> indices;    // Triangle indices (for visualization/debug)
        glm::vec3 center{0.0f};           // Hull center of mass
        float volume = 0.0f;              // Hull volume
    };

    // V-HACD decomposition parameters (stored with mesh for reproducibility)
    struct ConvexDecompositionParams
    {
        uint32_t maxConvexHulls = 16;          // Target max number of hulls
        uint32_t resolution = 100000;          // Voxel resolution (higher = more accurate, slower)
        uint32_t maxVerticesPerHull = 32;      // Max vertices per convex hull (Jolt limit is 256)
        float minVolumePercentError = 1.0f;    // Minimum volume error allowed (higher = fewer hulls)
        uint32_t maxRecursionDepth = 10;       // Max decomposition depth
        bool shrinkWrap = true;                // Project hull vertices to source mesh surface
    };

    // Collection of convex hulls for a submesh
    struct ConvexDecompositionData
    {
        bool hasDecomposition = false;
        ConvexDecompositionParams params;      // Parameters used for generation
        std::vector<ConvexHull> hulls;         // Generated convex hulls

        bool isValid() const
        {
            return hasDecomposition && !hulls.empty();
        }

        size_t getTotalVertexCount() const
        {
            size_t count = 0;
            for (const auto& hull : hulls)
            {
                count += hull.vertices.size();
            }
            return count;
        }
    };
}
