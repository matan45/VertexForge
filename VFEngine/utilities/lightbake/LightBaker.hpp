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

    using BakerProgressCallback = std::function<void(float progress)>;

    class LightBaker
    {
    public:
        LightBaker() = default;

        bool bake(const BakeSceneMesh& sceneMesh,
                  const LightmapAtlas& atlas,
                  const BakeLightSet& lights,
                  resource::LightmapData& outLightmap,
                  BakerProgressCallback progressCallback = nullptr);

        void cancel() { cancelled.store(true); }
        bool wasCancelled() const { return cancelled.load(); }
        void reset() { cancelled.store(false); }

    private:
        std::atomic<bool> cancelled{false};

        // Must match LIGHT_INTENSITY_SCALE in lighting_functions.glsl
        static constexpr float LIGHT_INTENSITY_SCALE = 100.0f;

        // Small offset to avoid self-intersection when casting shadow rays
        static constexpr float SHADOW_BIAS = 0.001f;

        static float smoothDistanceAttenuation(float distance, float range);
        static float physicalAttenuation(float distance, float range);
        static float spotAngleAttenuation(const glm::vec3& lightDir, const glm::vec3& spotDir,
                                          float cosInner, float cosOuter);

        bool bakeIrradianceMultithreaded(const std::vector<TexelSample>& texelSamples,
                                         const BakeLightSet& lights,
                                         const BakeSceneMesh& sceneMesh,
                                         resource::LightmapData& outLightmap,
                                         uint32_t validTexelCount,
                                         BakerProgressCallback progressCallback);

        static void dilateLightmap(resource::LightmapData& lightmap,
                                   const std::vector<TexelSample>& texelSamples);

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
