#pragma once
#include "BakeSceneMesh.hpp"
#include "LightmapAtlas.hpp"
#include "../components/LightTextComponents.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include <atomic>

namespace lightbake
{
    // Light data collected from the scene for baking
    struct BakeDirectionalLight
    {
        glm::vec3 direction{0.0f, -1.0f, 0.0f};
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
    };

    struct BakePointLight
    {
        glm::vec3 position{0.0f};
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        float radius = 10.0f;
    };

    struct BakeSpotLight
    {
        glm::vec3 position{0.0f};
        glm::vec3 direction{0.0f, -1.0f, 0.0f};
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        float range = 20.0f;
        float cosInnerAngle = 0.0f;
        float cosOuterAngle = 0.0f;
    };

    // All lights to bake
    struct BakeLightSet
    {
        std::vector<BakeDirectionalLight> directionalLights;
        std::vector<BakePointLight> pointLights;
        std::vector<BakeSpotLight> spotLights;

        bool empty() const
        {
            return directionalLights.empty() && pointLights.empty() && spotLights.empty();
        }
    };

    // Progress callback: reports 0.0 to 1.0
    using BakerProgressCallback = std::function<void(float progress)>;

    // CPU-based multi-threaded light baker.
    // Casts shadow rays from lightmap texel positions to light sources,
    // accumulates irradiance into the lightmap texture.
    class LightBaker
    {
    public:
        LightBaker() = default;

        // Bake direct irradiance into the lightmap.
        // Iterates all valid texel samples, casts shadow rays for each light,
        // and accumulates irradiance (color * intensity * NdotL * attenuation * shadow).
        //
        // The output is irradiance — the runtime shader multiplies by albedo.
        //
        // Returns true on success, false if cancelled or error.
        bool bake(const BakeSceneMesh& sceneMesh,
                  const LightmapAtlas& atlas,
                  const BakeLightSet& lights,
                  resource::LightmapData& outLightmap,
                  BakerProgressCallback progressCallback = nullptr);

        // Request cancellation of an in-progress bake
        void cancel() { cancelled_.store(true); }

        // Check if bake was cancelled
        bool wasCancelled() const { return cancelled_.load(); }

        // Reset cancellation state
        void reset() { cancelled_.store(false); }

    private:
        std::atomic<bool> cancelled_{false};

        // Must match LIGHT_INTENSITY_SCALE in lighting_functions.glsl
        static constexpr float LIGHT_INTENSITY_SCALE = 100.0f;

        // Small offset to avoid self-intersection when casting shadow rays
        static constexpr float SHADOW_BIAS = 0.001f;

        // Attenuation functions matching lighting_functions.glsl exactly
        static float smoothDistanceAttenuation(float distance, float range);
        static float physicalAttenuation(float distance, float range);
        static float spotAngleAttenuation(const glm::vec3& lightDir, const glm::vec3& spotDir,
                                          float cosInner, float cosOuter);

        // Per-texel irradiance accumulation
        glm::vec3 computeDirectionalIrradiance(const BakeDirectionalLight& light,
                                                const glm::vec3& worldPos,
                                                const glm::vec3& worldNormal,
                                                const BakeSceneMesh& sceneMesh) const;

        glm::vec3 computePointIrradiance(const BakePointLight& light,
                                          const glm::vec3& worldPos,
                                          const glm::vec3& worldNormal,
                                          const BakeSceneMesh& sceneMesh) const;

        glm::vec3 computeSpotIrradiance(const BakeSpotLight& light,
                                         const glm::vec3& worldPos,
                                         const glm::vec3& worldNormal,
                                         const BakeSceneMesh& sceneMesh) const;
    };
}
