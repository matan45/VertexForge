#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <variant>

namespace vfx
{
    enum class ForceSpace : uint8_t
    {
        World,
        Local
    };

    struct GravityForceConfig
    {
        glm::vec3 direction{0.0f, -1.0f, 0.0f};
        float strength = 9.81f;
        ForceSpace space = ForceSpace::World;
    };

    struct WindForceConfig
    {
        glm::vec3 direction{1.0f, 0.0f, 0.0f};
        float strength = 1.0f;
        float noiseStrength = 0.0f;
        float noiseFrequency = 1.0f;
        ForceSpace space = ForceSpace::World;
    };

    struct TurbulenceForceConfig
    {
        float strength = 1.0f;
        float frequency = 1.0f;
        float scrollSpeed = 0.0f;
        int octaves = 1;
        ForceSpace space = ForceSpace::World;
    };

    struct VortexForceConfig
    {
        glm::vec3 axis{0.0f, 1.0f, 0.0f};
        glm::vec3 center{0.0f, 0.0f, 0.0f};
        float strength = 1.0f;
        float radialPull = 0.0f;
        ForceSpace space = ForceSpace::World;
    };

    using VFXForceConfig = std::variant<
        GravityForceConfig,
        WindForceConfig,
        TurbulenceForceConfig,
        VortexForceConfig
    >;

    struct VFXForceChain
    {
        std::vector<VFXForceConfig> forces;

        bool empty() const { return forces.empty(); }
        size_t size() const { return forces.size(); }
    };
}
