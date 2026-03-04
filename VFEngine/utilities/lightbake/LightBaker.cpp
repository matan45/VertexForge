#include "LightBaker.hpp"
#include "../print/Log.hpp"
#include "../threading/JobSystem.hpp"
#include <thread>
#include <algorithm>
#include <cmath>

namespace lightbake
{
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

    glm::vec3 LightBaker::computeDirectionalIrradiance(
        const BakeDirectionalLight& light,
        const glm::vec3& worldPos,
        const glm::vec3& worldNormal,
        const BakeSceneMesh& sceneMesh) const
    {
        glm::vec3 lightDir = glm::normalize(-light.direction);
        float NdotL = glm::dot(worldNormal, lightDir);

        if (NdotL <= 0.0f)
        {
            return glm::vec3(0.0f);
        }

        math::Ray shadowRay(worldPos + worldNormal * SHADOW_BIAS, lightDir);
        if (sceneMesh.traceOcclusion(shadowRay))
        {
            return glm::vec3(0.0f);
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

        glm::vec3 lightDir = toLight / distance;
        float NdotL = glm::dot(worldNormal, lightDir);

        if (NdotL <= 0.0f)
        {
            return glm::vec3(0.0f);
        }

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

        float spotAtt = spotAngleAttenuation(lightDir, light.direction,
                                              light.cosInnerAngle, light.cosOuterAngle);
        if (spotAtt <= 0.0f)
        {
            return glm::vec3(0.0f);
        }

        math::Ray shadowRay(worldPos + worldNormal * SHADOW_BIAS, lightDir);
        if (sceneMesh.traceOcclusion(shadowRay, distance))
        {
            return glm::vec3(0.0f);
        }

        float distAtt = physicalAttenuation(distance, light.range);
        return light.color * light.intensity * distAtt * spotAtt * NdotL;
    }

    bool LightBaker::bake(
        const BakeSceneMesh& sceneMesh,
        const LightmapAtlas& atlas,
        const BakeLightSet& lights,
        resource::LightmapData& outLightmap,
        BakerProgressCallback progressCallback)
    {
        cancelled.store(false);

        if (!sceneMesh.isBuilt() || !atlas.isBuilt() || lights.empty())
        {
            vfLogWarning("[LightBake] Baker prerequisites not met");
            return false;
        }

        const auto& texelSamples = atlas.getTexelSamples();
        uint32_t width = atlas.getWidth();
        uint32_t height = atlas.getHeight();
        uint32_t totalTexels = width * height;

        outLightmap.width = width;
        outLightmap.height = height;
        outLightmap.channels = 3;
        outLightmap.texels.resize(totalTexels * 3, 0.0f);

        vfLogInfo("[LightBake] Starting bake: {}x{} atlas, {} dir + {} point + {} spot lights",
                     width, height,
                     lights.directionalLights.size(),
                     lights.pointLights.size(),
                     lights.spotLights.size());

        uint32_t validTexelCount = 0;
        for (const auto& sample : texelSamples)
        {
            if (sample.valid) ++validTexelCount;
        }

        if (validTexelCount == 0)
        {
            vfLogWarning("[LightBake] No valid texel samples to bake");
            return true;
        }

        vfLogInfo("[LightBake] {} valid texels out of {} total", validTexelCount, totalTexels);

        if (!bakeIrradianceMultithreaded(texelSamples, lights, sceneMesh, outLightmap,
                                          validTexelCount, progressCallback))
        {
            return false;
        }

        vfLogInfo("[LightBake] Bake complete: {} texels processed", validTexelCount);

        dilateLightmap(outLightmap, texelSamples);

        return true;
    }

    bool LightBaker::bakeIrradianceMultithreaded(
        const std::vector<TexelSample>& texelSamples,
        const BakeLightSet& lights,
        const BakeSceneMesh& sceneMesh,
        resource::LightmapData& outLightmap,
        uint32_t validTexelCount,
        BakerProgressCallback progressCallback)
    {
        uint32_t totalTexels = static_cast<uint32_t>(texelSamples.size());
        uint32_t threadCount = std::max(1u, std::thread::hardware_concurrency());
        uint32_t chunkSize = (totalTexels + threadCount - 1) / threadCount;

        std::atomic<uint32_t> processedTexels{0};
        std::vector<std::future<void>> futures;

        for (uint32_t t = 0; t < threadCount; ++t)
        {
            uint32_t start = t * chunkSize;
            uint32_t end = std::min(start + chunkSize, totalTexels);

            futures.push_back(threading::JobSystem::instance().submit(
                [this, &texelSamples, &lights, &sceneMesh, &outLightmap,
                 &processedTexels, validTexelCount, progressCallback,
                 start, end]()
                {
                    for (uint32_t i = start; i < end; ++i)
                    {
                        if (cancelled.load())
                            return;

                        const auto& sample = texelSamples[i];
                        if (!sample.valid)
                            continue;

                        glm::vec3 irradiance(0.0f);

                        for (const auto& light : lights.directionalLights)
                        {
                            irradiance += computeDirectionalIrradiance(
                                light, sample.worldPosition, sample.worldNormal, sceneMesh);
                        }

                        for (const auto& light : lights.pointLights)
                        {
                            irradiance += computePointIrradiance(
                                light, sample.worldPosition, sample.worldNormal, sceneMesh);
                        }

                        for (const auto& light : lights.spotLights)
                        {
                            irradiance += computeSpotIrradiance(
                                light, sample.worldPosition, sample.worldNormal, sceneMesh);
                        }

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
                }, threading::JobPriority::LOW
            ));
        }

        for (auto& f : futures)
        {
            f.get();
        }

        if (cancelled.load())
        {
            vfLogInfo("[LightBake] Bake cancelled");
            return false;
        }

        return true;
    }

    // Fills empty padding texels with nearest valid neighbor color
    // to prevent black seams when bilinear filtering samples across chart edges.
    void LightBaker::dilateLightmap(resource::LightmapData& lightmap,
                                    const std::vector<TexelSample>& texelSamples)
    {
        const uint32_t w = lightmap.width;
        const uint32_t h = lightmap.height;
        const uint32_t ch = lightmap.channels;

        std::vector<bool> valid(w * h, false);
        for (uint32_t i = 0; i < w * h; ++i)
        {
            if (texelSamples[i].valid)
                valid[i] = true;
        }

        const int dilationPasses = 4;
        for (int pass = 0; pass < dilationPasses; ++pass)
        {
            std::vector<bool> newValid = valid;
            for (uint32_t y = 0; y < h; ++y)
            {
                for (uint32_t x = 0; x < w; ++x)
                {
                    uint32_t idx = y * w + x;
                    if (valid[idx]) continue;

                    glm::vec3 sum(0.0f);
                    int count = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                    {
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            if (dx == 0 && dy == 0) continue;
                            int nx = static_cast<int>(x) + dx;
                            int ny = static_cast<int>(y) + dy;
                            if (nx < 0 || ny < 0 || nx >= static_cast<int>(w) || ny >= static_cast<int>(h)) continue;
                            uint32_t nIdx = ny * w + nx;
                            if (valid[nIdx])
                            {
                                sum.r += lightmap.texels[nIdx * ch + 0];
                                sum.g += lightmap.texels[nIdx * ch + 1];
                                sum.b += lightmap.texels[nIdx * ch + 2];
                                count++;
                            }
                        }
                    }

                    if (count > 0)
                    {
                        float inv = 1.0f / static_cast<float>(count);
                        lightmap.texels[idx * ch + 0] = sum.r * inv;
                        lightmap.texels[idx * ch + 1] = sum.g * inv;
                        lightmap.texels[idx * ch + 2] = sum.b * inv;
                        newValid[idx] = true;
                    }
                }
            }
            valid = std::move(newValid);
        }
    }
}
