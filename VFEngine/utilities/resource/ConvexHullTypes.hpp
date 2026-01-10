#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace resource
{
    struct ConvexHull
    {
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;  // For visualization/debug only
        glm::vec3 center{0.0f};
        float volume = 0.0f;
    };

    struct ConvexDecompositionParams
    {
        uint32_t maxConvexHulls = 16;
        uint32_t resolution = 100000;          // Higher = more accurate, slower
        uint32_t maxVerticesPerHull = 32;      // Jolt limit is 256
        float minVolumePercentError = 1.0f;    // Higher = fewer hulls
        uint32_t maxRecursionDepth = 10;
        bool shrinkWrap = true;
    };

    struct ConvexDecompositionData
    {
        bool hasDecomposition = false;
        ConvexDecompositionParams params;
        std::vector<ConvexHull> hulls;

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
