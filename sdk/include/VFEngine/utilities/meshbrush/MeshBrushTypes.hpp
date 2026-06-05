#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <glm/glm.hpp>
#include "../terrain/BrushTypes.hpp"

namespace meshbrush
{
    struct MeshPaletteEntry
    {
        std::string meshPath;
        std::string materialPath;
        float weight = 1.0f;
        glm::vec2 scaleRange{0.8f, 1.2f};
        glm::vec2 rotationYRange{0.0f, 360.0f};
        bool randomRotationX = false;
        bool randomRotationZ = false;
        bool alignToNormal = false;
        float maxSlope = 90.0f; // degrees
        float yOffset = 0.0f; // Manual vertical offset from terrain surface
        bool useCollider = false; // Add static box collider per instance
    };

    struct MeshBrushParams
    {
        float radius = 5.0f;
        float density = 1.0f;
        float spacing = 2.0f;
        bool continuousMode = true;
        float positionJitter = 0.5f;
        terrain::BrushFalloff falloff = terrain::BrushFalloff::Smooth;

        void validate()
        {
            radius = std::max(radius, 0.1f);
            density = std::clamp(density, 0.01f, 10.0f);
            spacing = std::max(spacing, 0.1f);
            positionJitter = std::clamp(positionJitter, 0.0f, 1.0f);
        }
    };

    enum class MeshBrushMode : uint8_t
    {
        Paint = 0,
        Erase = 1
    };
}
