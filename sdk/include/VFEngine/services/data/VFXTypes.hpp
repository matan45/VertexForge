#pragma once

#include <glm/glm.hpp>
#include <string>
#include <cstdint>
#include <optional>

namespace services
{
    using VFXInstanceId = uint32_t;

    enum class VFXEmitterPriority : uint8_t
    {
        Critical = 0,
        High = 1,
        Normal = 2,
        Low = 3
    };

    struct VFXRuntimeParams
    {
        std::string vfxAssetPath;
        glm::mat4 worldTransform{1.0f};
        bool loop = true;
        uint32_t entityId = 0;
        VFXEmitterPriority priority = VFXEmitterPriority::Normal;
        bool cameraRelative = false;
        // Destroy the instance automatically once a non-looping effect has
        // finished emitting and its last particles have expired (fire-and-forget)
        bool autoDestroy = false;
        // VK-1451 — deterministic seed for the emitter RNG. 0 => the renderer picks a
        // random seed once at creation (legacy behavior). A non-zero value makes the
        // instance's emission schedule reproducible across runs.
        uint32_t seed = 0;
    };

    struct VFXEmitterOverrides
    {
        std::optional<float> spawnRate;
        std::optional<float> lifetime;
        std::optional<float> startSize;
        std::optional<float> startSpeed;
        std::optional<float> stretchMultiplier;
        std::optional<glm::vec3> emitDirection;
        std::optional<glm::vec4> startColor;
        std::optional<glm::vec3> windDirection;
        std::optional<float> windStrength;
        std::optional<float> gravityStrength;
        std::optional<glm::vec3> gravityDirection;
        std::optional<int> renderMode;          // maps to VFXRenderMode
        std::optional<float> softParticleDistance;
        std::optional<float> lightingInfluence;
        std::optional<bool> collisionEnabled;
        std::optional<float> collisionLifetimeLoss;
        std::optional<glm::vec3> shapeDimensions; // box half-extents
        std::optional<float> coneSpread;          // emit direction cone angle in radians
    };

    struct VFXCameraParams
    {
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        glm::vec3 cameraPos{0.0f};
        float time = 0.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
    };
}
