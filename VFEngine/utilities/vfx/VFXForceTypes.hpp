#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <variant>

namespace vfx
{
    // Coordinate space for force application
    enum class ForceSpace : uint8_t
    {
        World,  // Force applies in world space
        Local   // Force transforms with emitter
    };

    // Gravity: Constant directional force
    struct GravityForceConfig
    {
        glm::vec3 direction{0.0f, -1.0f, 0.0f};  // Default: downward
        float strength = 9.81f;                   // Force magnitude
        ForceSpace space = ForceSpace::World;
    };

    // Wind: Directional force with optional noise variation
    struct WindForceConfig
    {
        glm::vec3 direction{1.0f, 0.0f, 0.0f};   // Default: +X
        float strength = 1.0f;                    // Force magnitude
        float noiseStrength = 0.0f;               // 0 = constant wind, >0 = variable
        float noiseFrequency = 1.0f;              // Spatial frequency of noise
        ForceSpace space = ForceSpace::World;
    };

    // Turbulence: Perlin/simplex noise-based chaotic movement
    struct TurbulenceForceConfig
    {
        float strength = 1.0f;      // Force magnitude
        float frequency = 1.0f;     // Noise spatial frequency
        float scrollSpeed = 0.0f;   // Noise time evolution speed
        int octaves = 1;            // Fractal octaves (1-4)
        ForceSpace space = ForceSpace::World;
    };

    // Vortex: Spiral force around an axis
    struct VortexForceConfig
    {
        glm::vec3 axis{0.0f, 1.0f, 0.0f};    // Rotation axis (default: Y-up)
        glm::vec3 center{0.0f, 0.0f, 0.0f};  // Vortex center position
        float strength = 1.0f;                // Tangential force magnitude
        float radialPull = 0.0f;              // Inward (negative) or outward (positive) force
        ForceSpace space = ForceSpace::World;
    };

    // Variant type for any force config
    using VFXForceConfig = std::variant<
        GravityForceConfig,
        WindForceConfig,
        TurbulenceForceConfig,
        VortexForceConfig
    >;

    // Ordered list of forces to apply
    struct VFXForceChain
    {
        std::vector<VFXForceConfig> forces;

        bool empty() const { return forces.empty(); }
        size_t size() const { return forces.size(); }
    };
}
