#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <string>
#include "../resource/Types.hpp"
#include "../resource/ConvexHullTypes.hpp"

namespace destruction
{
    enum class SeedDistribution : uint8_t
    {
        Uniform,
        Clustered,
        ArtistPlaced
    };

    struct ClusterParams
    {
        uint32_t clusterCount = 3;
        float clusterRadius = 0.2f;
    };

    struct FractureConfig
    {
        uint32_t cellCount = 10;
        SeedDistribution seedDistribution = SeedDistribution::Uniform;
        ClusterParams clusterParams;
        std::vector<glm::vec3> artistSeeds;
        uint32_t randomSeed = 42;
        float innerUVScale = 1.0f;
        bool generateConvexHulls = true;
        resource::ConvexDecompositionParams hullParams;
    };

    struct FragmentNeighbor
    {
        uint32_t neighborIndex;
        float sharedArea;
    };

    struct FragmentData
    {
        resource::MeshData mesh;
        resource::ConvexHull colliderHull;
        glm::vec3 centerOfMass{0.0f};
        float volume = 0.0f;
        std::vector<FragmentNeighbor> neighbors;
        uint32_t cellIndex = 0;
    };

    struct FractureResult
    {
        std::vector<FragmentData> fragments;
        bool success = false;
        std::string errorMessage;

        size_t getTotalVertexCount() const
        {
            size_t count = 0;
            for (const auto& frag : fragments)
            {
                for (const auto& lod : frag.mesh.lodLevels)
                {
                    count += lod.vertices.size();
                }
            }
            return count;
        }

        size_t getTotalIndexCount() const
        {
            size_t count = 0;
            for (const auto& frag : fragments)
            {
                for (const auto& lod : frag.mesh.lodLevels)
                {
                    count += lod.indices.size();
                }
            }
            return count;
        }
    };
}
