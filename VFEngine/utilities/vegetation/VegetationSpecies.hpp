#pragma once
#include <string>

namespace vegetation
{
    struct VegetationSpeciesConfig
    {
        std::string name;
        std::string meshPath;               // .vfMesh (contains LOD0-3 internally)
        std::string materialPath;           // .vfMat or .vfMatInstance

        float lod1Distance = 50.0f;          // LOD0 → LOD1 transition
        float lod2Distance = 100.0f;        // LOD1 → LOD2 transition
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
