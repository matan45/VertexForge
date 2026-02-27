#include "LightBaker.hpp"
#include <spdlog/spdlog.h>
#include <future>
#include <thread>
#include <algorithm>
#include <cmath>

namespace lightbake
{
    // -----------------------------------------------------------------------
    // Attenuation functions — must exactly match lighting_functions.glsl
    // -----------------------------------------------------------------------

    float LightBaker::smoothDistanceAttenuation(float distance, float range)
    {
        float distRatio = distance / range;
        float attenuation = std::clamp(1.0f - distRatio * distRatio, 0.0f, 1.0f);
        return attenuation * attenuation;
    }

    float LightBaker::physicalAttenuation(float distance, float range)
    {
        float windowFn = smoothDistanceAttenuation(distance, range);
        float distAtt = LIGHT_INTENSITY_SCALE / std::max(distance * distance, 0.0001f);
        return distAtt * windowFn;
    }

    float LightBaker::spotAngleAttenuation(const glm::vec3& lightDir, const glm::vec3& spotDir,
                                            float cosInner, float cosOuter)
    {
        float cosAngle = glm::dot(-lightDir, spotDir);

        if (cosInner <= cosOuter)
        {
            return cosAngle >= cosOuter ? 1.0f : 0.0f;
        }

        return std::clamp((cosAngle - cosOuter) / (cosInner - cosOuter), 0.0f, 1.0f);
    }

    // -----------------------------------------------------------------------
    // Per-texel irradiance computation
    // -----------------------------------------------------------------------

    glm::vec3 LightBaker::computeDirectionalIrradiance(
        const BakeDirectionalLight& light,
        const glm::vec3& worldPos,
        const glm::vec3& worldNormal,
        const BakeSceneMesh& sceneMesh) const
    {
        glm::vec3 lightDir = glm::normalize(-light.direction); // Direction toward light
        float NdotL = glm::dot(worldNormal, lightDir);

        if (NdotL <= 0.0f)
        {
            return glm::vec3(0.0f);
        }

        // Cast shadow ray from surface toward light
        math::Ray shadowRay(worldPos + worldNormal * SHADOW_BIAS, lightDir);
        if (sceneMesh.traceOcclusion(shadowRay))
        {
            return glm::vec3(0.0f); // In shadow
        }

        return light.color * light.intensity * NdotL;
    }

    glm::vec3 LightBaker::computePointIrradiance(
        const BakePointLight& light,
        const glm::vec3& worldPos,
        const glm::vec3& worldNormal,
        const BakeSceneMesh& sceneMesh) const
    {
        glm::vec3 toLight = light.position - worldPos;
        float distance = glm::length(toLight);

        if (distance > light.radius || distance < 0.001f)
        {
            return glm::vec3(0.0f);
        }

        glm::vec3 lightDir = toLight / distance; // Normalized direction toward light
        float NdotL = glm::dot(worldNormal, lightDir);

        if (NdotL <= 0.0f)
        {
            return glm::vec3(0.0f);
        }

        // Shadow ray
        math::Ray shadowRay(worldPos + worldNormal * SHADOW_BIAS, lightDir);
        if (sceneMesh.traceOcclusion(shadowRay, distance))
        {
            return glm::vec3(0.0f);
        }

        float attenuation = physicalAttenuation(distance, light.radius);
        return light.color * light.intensity * attenuation * NdotL;
    }

    glm::vec3 LightBaker::computeSpotIrradiance(
        const BakeSpotLight& light,
        const glm::vec3& worldPos,
        const glm::vec3& worldNormal,
        const BakeSceneMesh& sceneMesh) const
    {
        glm::vec3 toLight = light.position - worldPos;
        float distance = glm::length(toLight);

        if (distance > light.range || distance < 0.001f)
        {
            return glm::vec3(0.0f);
        }

        glm::vec3 lightDir = toLight / distance;
        float NdotL = glm::dot(worldNormal, lightDir);

        if (NdotL <= 0.0f)
        {
            return glm::vec3(0.0f);
        }

        // Spot cone attenuation
        float spotAtt = spotAngleAttenuation(lightDir, light.direction,
                                              light.cosInnerAngle, light.cosOuterAngle);
        if (spotAtt <= 0.0f)
        {
            return glm::vec3(0.0f);
        }

        // Shadow ray
        math::Ray shadowRay(worldPos + worldNormal * SHADOW_BIAS, lightDir);
        if (sceneMesh.traceOcclusion(shadowRay, distance))
        {
            return glm::vec3(0.0f);
        }

        float distAtt = physicalAttenuation(distance, light.range);
        return light.color * light.intensity * distAtt * spotAtt * NdotL;
    }

    // -----------------------------------------------------------------------
    // Main bake function
    // -----------------------------------------------------------------------

    bool LightBaker::bake(
        const BakeSceneMesh& sceneMesh,
        const LightmapAtlas& atlas,
        const BakeLightSet& lights,
        resource::LightmapData& outLightmap,
        BakerProgressCallback progressCallback)
    {
        cancelled_.store(false);

        if (!sceneMesh.isBuilt() || !atlas.isBuilt() || lights.empty())
        {
            spdlog::warn("[LightBake] Baker prerequisites not met");
            return false;
        }

        const auto& texelSamples = atlas.getTexelSamples();
        uint32_t width = atlas.getWidth();
        uint32_t height = atlas.getHeight();
        uint32_t totalTexels = width * height;

        // Ensure output lightmap has correct dimensions
        outLightmap.width = width;
        outLightmap.height = height;
        outLightmap.channels = 3;
        outLightmap.texels.resize(totalTexels * 3, 0.0f);

        spdlog::info("[LightBake] Starting bake: {}x{} atlas, {} dir + {} point + {} spot lights",
                     width, height,
                     lights.directionalLights.size(),
                     lights.pointLights.size(),
                     lights.spotLights.size());

        // Count valid texels for progress reporting
        uint32_t validTexelCount = 0;
        for (const auto& sample : texelSamples)
        {
            if (sample.valid) ++validTexelCount;
        }

        if (validTexelCount == 0)
        {
            spdlog::warn("[LightBake] No valid texel samples to bake");
            return true; // Not an error — just nothing to bake
        }

        spdlog::info("[LightBake] {} valid texels out of {} total", validTexelCount, totalTexels);

        // Multi-threaded bake: divide texels into chunks
        uint32_t threadCount = std::max(1u, std::thread::hardware_concurrency());
        uint32_t chunkSize = (totalTexels + threadCount - 1) / threadCount;

        std::atomic<uint32_t> processedTexels{0};
        std::vector<std::future<void>> futures;

        for (uint32_t t = 0; t < threadCount; ++t)
        {
            uint32_t start = t * chunkSize;
            uint32_t end = std::min(start + chunkSize, totalTexels);

            futures.push_back(std::async(std::launch::async,
                [this, &texelSamples, &lights, &sceneMesh, &outLightmap,
                 &processedTexels, validTexelCount, progressCallback,
                 start, end]()
                {
                    for (uint32_t i = start; i < end; ++i)
                    {
                        if (cancelled_.load())
                        {
                            return;
                        }

                        const auto& sample = texelSamples[i];
                        if (!sample.valid)
                        {
                            continue;
                        }

                        glm::vec3 irradiance(0.0f);

                        // Accumulate directional lights
                        for (const auto& light : lights.directionalLights)
                        {
                            irradiance += computeDirectionalIrradiance(
                                light, sample.worldPosition, sample.worldNormal, sceneMesh);
                        }

                        // Accumulate point lights
                        for (const auto& light : lights.pointLights)
                        {
                            irradiance += computePointIrradiance(
                                light, sample.worldPosition, sample.worldNormal, sceneMesh);
                        }

                        // Accumulate spot lights
                        for (const auto& light : lights.spotLights)
                        {
                            irradiance += computeSpotIrradiance(
                                light, sample.worldPosition, sample.worldNormal, sceneMesh);
                        }

                        // Write to lightmap (no race — each texel written by one thread)
                        uint32_t idx = i * 3;
                        outLightmap.texels[idx + 0] = irradiance.r;
                        outLightmap.texels[idx + 1] = irradiance.g;
                        outLightmap.texels[idx + 2] = irradiance.b;

                        uint32_t done = processedTexels.fetch_add(1) + 1;
                        if (progressCallback && (done % 1000 == 0 || done == validTexelCount))
                        {
                            progressCallback(static_cast<float>(done) / static_cast<float>(validTexelCount));
                        }
                    }
                }
            ));
        }

        // Wait for all threads
        for (auto& f : futures)
        {
            f.get();
        }

        if (cancelled_.load())
        {
            spdlog::info("[LightBake] Bake cancelled");
            return false;
        }

        spdlog::info("[LightBake] Bake complete: {} texels processed", validTexelCount);
        return true;
    }
}
