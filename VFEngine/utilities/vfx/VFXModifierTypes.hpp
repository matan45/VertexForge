#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <variant>

namespace vfx
{
    struct ColorOverLifetimeConfig
    {
        glm::vec4 startColor{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 endColor{1.0f, 1.0f, 1.0f, 0.0f};
    };

    struct SizeOverLifetimeConfig
    {
        float startMultiplier = 1.0f;
        float endMultiplier = 0.0f;
    };

    struct SpeedOverLifetimeConfig
    {
        float startMultiplier = 1.0f;
        float endMultiplier = 0.5f;
    };

    struct RotationOverLifetimeConfig
    {
        float angularVelocity = 0.0f;
    };

    using VFXModifierConfig = std::variant<
        ColorOverLifetimeConfig,
        SizeOverLifetimeConfig,
        SpeedOverLifetimeConfig,
        RotationOverLifetimeConfig
    >;

    struct VFXModifierChain
    {
        std::vector<VFXModifierConfig> modifiers;

        bool empty() const { return modifiers.empty(); }
        size_t size() const { return modifiers.size(); }
    };
}
