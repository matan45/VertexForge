#pragma once
#include <glm/glm.hpp>
#include <string>
#include <cstdint>
#include "../asset/AssetRef.hpp"

namespace components
{
    struct DirectionalLightComponent
    {
        glm::vec3 color{1.0f, 1.0f, 1.0f};
        float intensity{1.0f};
        float lightSize{1.0f};
        bool showGizmo = false;
    };

    struct PointLightComponent
    {
        glm::vec3 color{1.0f, 1.0f, 1.0f};
        float intensity{1.0f};
        float radius{10.0f};
        float lightSize{0.1f};
        bool castsShadow = false;
        bool showGizmo = false;
    };

    struct SpotLightComponent
    {
        glm::vec3 color{1.0f, 1.0f, 1.0f};
        float intensity{1.0f};
        float innerAngle{30.0f};
        float outerAngle{45.0f};
        float range{20.0f};
        float lightSize{0.1f};
        bool castsShadow = false;
        bool showGizmo = false;
    };

    struct TextComponent
    {
        asset::AssetRef fontRef;
        std::string text = "Hello World";
        float fontSize = 32.0f;
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        float lineSpacing = 1.0f;
        float letterSpacing = 0.0f;
        float maxWidth = 0.0f;
    };

}
