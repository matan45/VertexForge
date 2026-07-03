#pragma once

#include <glm/glm.hpp>
#include <cstdint>
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

    // Velocity damping: v /= (1 + (linearCoeff + quadraticCoeff * |v|) * dt).
    // Applied multiplicatively (semi-implicit) so it never reverses velocity, even at large dt.
    struct DragForceConfig
    {
        float linearCoeff = 1.0f;
        float quadraticCoeff = 0.0f;
        ForceSpace space = ForceSpace::World; // stored for parity; damping is space-independent
    };

    // Pull toward a world-space point. Negative strength = repulsor.
    struct PointAttractorForceConfig
    {
        glm::vec3 position{0.0f, 0.0f, 0.0f}; // world-space (like VortexForceConfig::center)
        float strength = 5.0f;
        float radius = 10.0f;                 // influence cutoff; no force beyond
        float falloff = 1.0f;                 // 1 = linear, 2 = quadratic ease toward center
        bool killAtCenter = false;            // kill particles that reach the center
        ForceSpace space = ForceSpace::World; // stored for parity; center is authored in world space
    };

    // Divergence-free curl noise: F = curl of a 3-offset simplex vector potential
    // (Bridson 2007). Parameters mirror TurbulenceForceConfig. See VFXCurlNoise.hpp.
    struct CurlNoiseForceConfig
    {
        float strength = 1.0f;
        float frequency = 1.0f;
        float scrollSpeed = 0.0f;
        int octaves = 1;
        ForceSpace space = ForceSpace::World; // stored for parity; field is sampled in world space
    };

    enum class KillVolumeShape : uint8_t
    {
        Plane = 0,
        Sphere = 1,
        Box = 2
    };

    // Kill predicate force: no acceleration, only deactivates particles after integration.
    struct KillVolumeForceConfig
    {
        KillVolumeShape shape = KillVolumeShape::Plane;
        glm::vec3 center{0.0f};                 // plane point / sphere center / box center
        glm::vec3 normal{0.0f, 1.0f, 0.0f};     // plane normal; kills negative side by default
        float radius = 1.0f;                    // sphere radius
        glm::vec3 halfExtents{1.0f};            // axis-aligned box half extents
        bool invert = false;                    // flip the kill side/inside predicate
        ForceSpace space = ForceSpace::World;
    };

    using VFXForceConfig = std::variant<
        GravityForceConfig,
        WindForceConfig,
        TurbulenceForceConfig,
        VortexForceConfig,
        DragForceConfig,
        PointAttractorForceConfig,
        CurlNoiseForceConfig,
        KillVolumeForceConfig
    >;

    struct VFXForceChain
    {
        std::vector<VFXForceConfig> forces;

        bool empty() const { return forces.empty(); }
        size_t size() const { return forces.size(); }
    };
}
