#pragma once
#include <string>

namespace vegetation
{
    struct VegetationSpeciesConfig
    {
        std::string name;
        std::string meshPath;               // .vfMesh (contains LOD0-3 internally)
        std::string materialPath;           // .vfMat or .vfMatInstance
        std::string imposterAtlasPath;      // Billboard imposter atlas (empty = no imposter LOD)

        float imposterDistance = 150.0f;     // Distance to switch from mesh to imposter billboard
        float maxRenderDistance = 500.0f;    // Beyond this, not rendered

        float minScale = 0.8f;
        float maxScale = 1.2f;
        float windStrength = 1.0f;

        // GPU Billboard Impostor
        bool useGPUBillboardImposters = true;  // Use GPU billboard pipeline for impostor LOD

        // Collision
        bool hasCollision = false;
        float collisionRadius = 0.3f;
        float collisionHeight = 5.0f;
    };
}
