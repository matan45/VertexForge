#pragma once

#include <cstdint>
#include <algorithm>
#include <string>
#include <glm/glm.hpp>

namespace terrain
{
    enum class BrushType : uint8_t
    {
        Raise = 0,
        Lower = 1,
        Smooth = 2,
        Flatten = 3,
        Noise = 4,
        Stamp = 5
    };

    enum class BrushFalloff : uint8_t
    {
        Constant = 0,
        Linear = 1,
        Smooth = 2,
        Sharp = 3
    };

    enum class BrushShape : uint8_t
    {
        Circle = 0
    };

    struct BrushParams
    {
        float radius = 5.0f;
        float strength = 10.0f;
        BrushFalloff falloff = BrushFalloff::Smooth;
        BrushShape shape = BrushShape::Circle;
        float stampRotation = 0.0f;
        float stampScale = 1.0f;
        std::string stampImagePath;

        void validate()
        {
            radius = std::max(radius, 0.1f);
            strength = std::clamp(strength, 0.0f, 100.0f);
        }
    };

    struct BrushGPUParams
    {
        glm::vec2 brushCenter{0.0f};
        glm::vec2 tileWorldOrigin{0.0f};
        float brushRadius = 0.0f;
        float brushStrength = 0.0f;
        float vertexSpacing = 0.0f;
        uint32_t verticesPerSide = 0;
        BrushFalloff falloff = BrushFalloff::Smooth;
        BrushShape shape = BrushShape::Circle;
        BrushType brushType = BrushType::Raise;
        float deltaTime = 0.0f;
        float targetHeight = 0.0f;
        float minHeight = 0.0f;
        float maxHeight = 0.0f;
        bool invert = false;
        float stampRotation = 0.0f;
        float stampScale = 1.0f;
        uint32_t stampWidth = 0;
        uint32_t stampHeight = 0;
    };
}
