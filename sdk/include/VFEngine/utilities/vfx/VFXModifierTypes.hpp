#pragma once

#include "VFXCurveTypes.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <variant>

namespace vfx
{
    struct ColorOverLifetimeConfig
    {
        VFXGradient gradient = VFXGradient::fromStartEnd(
            glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    };

    struct SizeOverLifetimeConfig
    {
        VFXCurve curve = VFXCurve::fromStartEnd(1.0f, 0.0f);
    };

    struct SpeedOverLifetimeConfig
    {
        VFXCurve curve = VFXCurve::fromStartEnd(1.0f, 0.5f);
    };

    struct RotationOverLifetimeConfig
    {
        VFXCurve curve = VFXCurve::constant(0.0f);
    };

    struct GlowOverLifetimeConfig
    {
        VFXCurve curve = VFXCurve::fromStartEnd(1.0f, 0.0f);
        glm::vec3 glowColor{1.0f, 1.0f, 1.0f};
    };

    // VK-1473: sampled by normalized particle speed (length(velocity) remapped over
    // [speedMin, speedMax]) instead of age, and MULTIPLIED on top of the over-lifetime result.
    struct SizeBySpeedConfig
    {
        VFXCurve curve = VFXCurve::fromStartEnd(0.5f, 1.0f); // smaller when slow, full when fast
        float speedMin = 0.0f;
        float speedMax = 10.0f;
    };

    struct ColorBySpeedConfig
    {
        VFXGradient gradient = VFXGradient::fromStartEnd(
            glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
        float speedMin = 0.0f;
        float speedMax = 10.0f;
    };

    using VFXModifierConfig = std::variant<
        ColorOverLifetimeConfig,
        SizeOverLifetimeConfig,
        SpeedOverLifetimeConfig,
        RotationOverLifetimeConfig,
        GlowOverLifetimeConfig,
        SizeBySpeedConfig,
        ColorBySpeedConfig
    >;

    struct VFXModifierChain
    {
        std::vector<VFXModifierConfig> modifiers;

        bool empty() const { return modifiers.empty(); }
        size_t size() const { return modifiers.size(); }
    };
}
