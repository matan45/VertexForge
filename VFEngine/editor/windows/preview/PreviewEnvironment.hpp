#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace editor::preview
{
    enum class BackgroundMode : uint8_t
    {
        SolidColor,
        Gradient
    };

    enum class LightingMode : uint8_t
    {
        Default,
        ThreePoint
    };

    struct PreviewEnvironment
    {
        BackgroundMode backgroundMode = BackgroundMode::SolidColor;
        glm::vec4 backgroundColor{ 0.15f, 0.15f, 0.15f, 1.0f };
        glm::vec4 gradientTopColor{ 0.165f, 0.184f, 0.271f, 1.0f };
        glm::vec4 gradientBottomColor{ 0.106f, 0.118f, 0.169f, 1.0f };
        bool showGrid = true;
        LightingMode lightingMode = LightingMode::Default;
        float lightingIntensity = 1.0f;
    };
}
