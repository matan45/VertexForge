#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <variant>

namespace vfx
{
    // Color Over Lifetime modifier config
    struct ColorOverLifetimeConfig
    {
        glm::vec4 startColor{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 endColor{1.0f, 1.0f, 1.0f, 0.0f};
    };

    // Size Over Lifetime modifier config
    struct SizeOverLifetimeConfig
    {
        float startMultiplier = 1.0f;
        float endMultiplier = 0.0f;
    };

    // Speed Over Lifetime modifier config
    struct SpeedOverLifetimeConfig
    {
        float startMultiplier = 1.0f;
        float endMultiplier = 0.5f;
    };

    // Rotation Over Lifetime modifier config
    struct RotationOverLifetimeConfig
    {
        float angularVelocity = 0.0f;  // Degrees per second
    };

    // Variant type for any modifier config
    using VFXModifierConfig = std::variant<
        ColorOverLifetimeConfig,
        SizeOverLifetimeConfig,
        SpeedOverLifetimeConfig,
        RotationOverLifetimeConfig
    >;

    // Ordered list of modifiers to apply
    struct VFXModifierChain
    {
        std::vector<VFXModifierConfig> modifiers;

        bool empty() const { return modifiers.empty(); }
        size_t size() const { return modifiers.size(); }
    };
}
