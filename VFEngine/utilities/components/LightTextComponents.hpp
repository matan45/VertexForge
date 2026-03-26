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

    struct ShadowOverrideComponent
    {
        float depthBias = -1.0f;
        float slopeBias = -1.0f;
        float normalBias = -1.0f;
        uint32_t maxPages = 0;
        bool softShadows = false;
        bool hasSoftShadowOverride = false;
    };

    enum class FogVolumeShape : uint8_t
    {
        Box = 0,
        Sphere = 1,
        Cylinder = 2
    };

    enum class FogVolumeBlendMode : uint8_t
    {
        Additive = 0,
        Subtractive = 1
    };

    struct FogVolumeComponent
    {
        FogVolumeShape shape = FogVolumeShape::Box;
        glm::vec3 halfExtents{5.0f};
        float density = 0.5f;
        glm::vec3 albedo{0.8f, 0.85f, 0.9f};
        glm::vec3 emission{0.0f};
        float edgeFalloff = 0.5f;
        FogVolumeBlendMode blendMode = FogVolumeBlendMode::Additive;
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
