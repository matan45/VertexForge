#pragma once
#include <string>

namespace vegetation
{
    struct VegetationSpeciesConfig
    {
        std::string name;
        std::string meshPathLOD0;           // Full detail mesh
        std::string meshPathLOD1;           // Simplified mesh (empty = no LOD1)
        std::string imposterAtlasPath;      // Billboard imposter atlas (empty = no LOD2)

        float lodDistance0to1 = 50.0f;       // Distance for LOD0 -> LOD1 transition
        float lodDistance1to2 = 150.0f;      // Distance for LOD1 -> LOD2 transition
        float maxRenderDistance = 500.0f;    // Beyond this, not rendered

        float minScale = 0.8f;
        float maxScale = 1.2f;
        float windStrength = 1.0f;

        // Collision
        bool hasCollision = false;
        float collisionRadius = 0.3f;
        float collisionHeight = 5.0f;
    };
}
