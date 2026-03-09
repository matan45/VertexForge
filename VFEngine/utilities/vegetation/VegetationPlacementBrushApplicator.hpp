#pragma once

#include "VegetationPlacementData.hpp"
#include "VegetationTypes.hpp"
#include <glm/glm.hpp>

namespace vegetation
{
    class VegetationPlacementBrushApplicator
    {
    public:
        struct PlaceParams
        {
            glm::vec3 brushPosition;     // World position of brush hit
            glm::vec2 tileWorldOrigin;   // World XZ origin of this tile
            float minScale;
            float maxScale;
            float randomRotation;        // [0,1] - amount of random Y rotation
            uint32_t speciesId;
            float tileWorldSize;
            float collisionRadius = 0.0f; // If > 0, minimum spacing between instances

            // Terrain height sampling data
            const float* heightData = nullptr;  // Tile heightfield grid
            uint32_t heightVertexCount = 0;      // Vertices per side in heightfield
        };

        struct SpreadParams
        {
            glm::vec2 brushCenter;       // World XZ position of brush center
            glm::vec2 tileWorldOrigin;   // World XZ origin of this tile
            float brushRadius;
            float density;               // Controls grid spacing (instances per unit area)
            float minScale;
            float maxScale;
            float randomRotation;        // [0,1] - amount of random Y rotation
            uint32_t speciesId;
            float tileWorldSize;
            float collisionRadius = 0.0f; // If > 0, minimum spacing between instances

            // Terrain height sampling data
            const float* heightData = nullptr;
            uint32_t heightVertexCount = 0;
        };

        struct EraseParams
        {
            glm::vec3 brushCenter3D;     // 3D world position
            float brushRadius;
        };

        // Place a single vegetation instance at the brush hit position.
        // Returns true if an instance was added.
        static bool place(VegetationPlacementData& placement, const PlaceParams& params);

        // Spread vegetation instances within brush radius using deterministic grid.
        // Won't place on top of existing instances. Returns true if any instances were added.
        static bool spread(VegetationPlacementData& placement, const SpreadParams& params);

        // Erase vegetation instances within brush radius. Returns true if any instances were removed.
        static bool erase(VegetationPlacementData& placement, const EraseParams& params);

        // Sample terrain height at a world XZ position using bilinear interpolation
        static float sampleTerrainHeight(float worldX, float worldZ,
            const glm::vec2& tileWorldOrigin, float tileWorldSize,
            const float* heightData, uint32_t vertexCount);

    private:
        // Generate a simple random float [0,1]
        static float randomFloat();

        // Deterministic hash from grid cell coordinates
        static uint32_t cellHash(int cellX, int cellZ, uint32_t seed = 0);

        // Float [0,1] from hash value
        static float hashToFloat(uint32_t h);
    };
}
