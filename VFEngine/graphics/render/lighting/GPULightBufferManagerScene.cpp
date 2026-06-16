#include "GPULightBufferManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../shadow/ShadowSystem.hpp"
#include "../shadow/ShadowTypes.hpp"
#include "print/Log.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"
#include "components/LightTextComponents.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace render::lighting
{
    namespace
    {
        // Shared RT-slice assignment for both spot and point lights: the two GPU light structs
        // expose .position/.intensity/.shadowIndex/.rtMaskSlice identically, and the budget array
        // size N is the same (MAX_RT_SPOT_LIGHTS == MAX_RT_POINT_LIGHTS == 8). Selects the
        // closest/brightest shadow-casting lights (up to budget, capped at N), assigns each a stable
        // RT mask slice (keeping a light on its prior slice to avoid invalidating denoiser history),
        // and returns the slices whose owner changed this frame (history must be reset for those).
        // needsUpload is set only on the main assignment path, matching the original behaviour.
        template <typename LightT, size_t N>
        std::vector<uint32_t> assignRTSlicesGeneric(
            std::vector<LightT>& cpuLights,
            const std::vector<uint32_t>& cpuEntityIds,
            uint32_t count,
            std::array<uint32_t, N>& sliceLightsState,
            std::array<bool, N>& sliceValidState,
            uint32_t budget,
            const glm::vec3& cameraPos,
            bool& needsUpload)
        {
            // Clear any prior assignment so a light that drops out of the budget this frame
            // falls straight back to VSM (no stale mask slice).
            for (uint32_t i = 0; i < count; ++i)
                cpuLights[i].rtMaskSlice = -1;

            const uint32_t cap = std::min(budget, static_cast<uint32_t>(N));
            if (cap == 0)
            {
                sliceLightsState = {};
                return {};
            }

            // Score shadow-casting lights by brightness / distance² (closest + brightest win).
            struct Candidate
            {
                uint32_t index;
                uint32_t entityId;
                float score;
            };
            std::vector<Candidate> candidates;
            candidates.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                const LightT& l = cpuLights[i];
                if (l.shadowIndex < 0)
                    continue; // only lights that already have a VSM shadow view are RT-eligible
                const float distSq = std::max(glm::dot(l.position - cameraPos, l.position - cameraPos), 1e-4f);
                candidates.push_back({i, cpuEntityIds[i], l.intensity / distSq});
            }

            if (candidates.empty())
            {
                sliceLightsState = {};
                return {};
            }

            const uint32_t take = std::min(cap, static_cast<uint32_t>(candidates.size()));
            std::partial_sort(candidates.begin(), candidates.begin() + take, candidates.end(),
                              [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

            // Slice stability: keep a light on its previous slice when it stays in the budget, so the
            // per-slice denoiser history isn't invalidated by churn. New winners fill the freed slots.
            std::array<uint32_t, N> sliceLights{};
            std::array<bool, N> sliceUsed{};
            sliceLights.fill(0);
            sliceUsed.fill(false);

            std::vector<Candidate> pendingNew;
            for (uint32_t k = 0; k < take; ++k)
            {
                const Candidate& c = candidates[k];
                int prevSlice = -1;
                for (uint32_t s = 0; s < cap; ++s)
                {
                    if (sliceLightsState[s] == c.entityId && sliceValidState[s])
                    {
                        prevSlice = static_cast<int>(s);
                        break;
                    }
                }
                if (prevSlice >= 0 && !sliceUsed[prevSlice])
                {
                    sliceUsed[prevSlice] = true;
                    sliceLights[prevSlice] = c.entityId;
                    cpuLights[c.index].rtMaskSlice = prevSlice;
                }
                else
                {
                    pendingNew.push_back(c);
                }
            }

            std::vector<uint32_t> reassignedSlices;
            for (const Candidate& c : pendingNew)
            {
                for (uint32_t s = 0; s < cap; ++s)
                {
                    if (!sliceUsed[s])
                    {
                        sliceUsed[s] = true;
                        sliceLights[s] = c.entityId;
                        cpuLights[c.index].rtMaskSlice = static_cast<int>(s);
                        reassignedSlices.push_back(s); // history for this slice must be reset
                        break;
                    }
                }
            }

            for (uint32_t s = 0; s < cap; ++s)
            {
                sliceLightsState[s] = sliceLights[s];
                sliceValidState[s] = sliceUsed[s];
            }
            for (uint32_t s = cap; s < static_cast<uint32_t>(N); ++s)
                sliceValidState[s] = false;

            needsUpload = true;
            return reassignedSlices;
        }
    }

    void GPULightBufferManager::updateFromScene()
    {
        updateFromScene(std::unordered_set<uint32_t>{});
    }

    void GPULightBufferManager::updateFromScene(const std::unordered_set<uint32_t>& visibleLightIds)
    {
        if (!initialized)
        {
            return;
        }

        const std::unordered_set<uint32_t>* filterPtr = visibleLightIds.empty() ? nullptr : &visibleLightIds;

        pendingDirShadow.clear();
        pendingPointShadow.clear();
        pendingSpotShadow.clear();

        // Collect sequentially: EnTT registry is not thread-safe for concurrent view
        // iteration, even read-only. The work per light type is trivial (iterate entities,
        // copy data to staging arrays), so parallelism adds more overhead than benefit.
        collectDirectionalLights(filterPtr, pendingDirShadow);
        collectPointLights(filterPtr, pendingPointShadow);
        appendTransientPointLights();
        collectSpotLights(filterPtr, pendingSpotShadow);

        processPendingShadowRegistrations();
        cleanupStaleShadowRegistrations();

        if (detectChanges())
        {
            updateCountsBuffer();
            needsUpload = true;
        }
    }

    void GPULightBufferManager::updateShadowRegistration(uint32_t entityId,
                                                          shadow::ShadowMapType type,
                                                          const shadow::ShadowSettings& settings)
    {
        bool isRegistered = registeredShadowLights.contains(entityId);
        bool shadowsEnabled = shadowSystem->isShadowsEnabled();

        if (shadowsEnabled)
        {
            if (shadowSystem->registerLight(entityId, type, settings))
            {
                registeredShadowLights.insert(entityId);
            }
        }
        else if (isRegistered)
        {
            shadowSystem->unregisterLight(entityId);
            registeredShadowLights.erase(entityId);
        }
    }

    void GPULightBufferManager::collectDirectionalLights(const std::unordered_set<uint32_t>* visibleLightIds,
                                                          std::vector<PendingShadowReg>& pendingShadow)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::DirectionalLightComponent, components::WorldTransformComponent>();

        directionalCount = 0;
        bool hitLimit = false;

        for (auto entity : view)
        {
            if (!scene::Entity::isEffectivelyActive(registry, entity))
                continue;

            if (visibleLightIds && visibleLightIds->find(static_cast<uint32_t>(entity)) == visibleLightIds->end())
                continue;

            if (directionalCount >= LightConstants::MAX_DIRECTIONAL_LIGHTS)
            {
                hitLimit = true;
                break;
            }

            const auto& light = view.get<components::DirectionalLightComponent>(entity);

            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);
            glm::vec3 direction = glm::normalize(glm::vec3(worldTransform.worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

            uint32_t entityId = static_cast<uint32_t>(entity);

            // Register a directional Virtual Shadow Map clipmap. This is the unified default
            // directional shadow (works on all GPUs, covers the full streamed range); the RT
            // path, when enabled, overrides it full-screen via lightCounts.rtShadowActive.
            if (shadowSystem)
            {
                shadow::ShadowSettings settings{};
                settings.depthBias = shadowSystem->getGlobalDepthBias();
                settings.normalBias = shadowSystem->getGlobalNormalBias();
                settings.lightSize = light.lightSize;
                settings.clipmapLevelCount = shadowSystem->getClipmapLevelCount();
                settings.clipmapBaseExtent = shadowSystem->getClipmapBaseExtent();
                settings.clipmapDepthRange = shadowSystem->getClipmapDepthRange();
                settings.enabled = true;
                settings.castShadows = true;
                pendingShadow.push_back({entityId, shadow::ShadowMapType::Directional, settings});
            }

            GPUDirectionalLight& gpuLight = cpuDirectionalLights[directionalCount];
            gpuLight.direction = direction;
            gpuLight.intensity = light.intensity;
            gpuLight.color = light.color;
            gpuLight.shadowIndex = shadowSystem ? shadowSystem->getShadowViewIndex(entityId) : -1;
            gpuLight.shadowMode = 1; // 1 = VSM clipmap

            ++directionalCount;
        }

        if (hitLimit && !warnedDirectionalLimit)
        {
            vfLogWarning("GPULightBufferManager: Exceeded max directional lights ({}). Additional lights will be ignored.",
                          LightConstants::MAX_DIRECTIONAL_LIGHTS);
            warnedDirectionalLimit = true;
        }
        else if (!hitLimit && warnedDirectionalLimit)
        {
            warnedDirectionalLimit = false;
        }
    }

    void GPULightBufferManager::collectPointLights(const std::unordered_set<uint32_t>* visibleLightIds,
                                                     std::vector<PendingShadowReg>& pendingShadow)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::PointLightComponent, components::WorldTransformComponent>();

        pointCount = 0;
        bool hitLimit = false;

        for (auto entity : view)
        {
            if (!scene::Entity::isEffectivelyActive(registry, entity))
                continue;

            if (visibleLightIds && visibleLightIds->find(static_cast<uint32_t>(entity)) == visibleLightIds->end())
            {
                continue;
            }

            if (pointCount >= LightConstants::MAX_POINT_LIGHTS)
            {
                hitLimit = true;
                break;
            }

            const auto& light = view.get<components::PointLightComponent>(entity);

            uint32_t entityId = static_cast<uint32_t>(entity);
            if (shadowSystem && light.castsShadow)
            {
                shadow::ShadowSettings settings{};
                settings.depthBias = shadowSystem->getGlobalDepthBias();
                settings.normalBias = shadowSystem->getGlobalNormalBias();
                settings.farPlane = light.radius;
                settings.lightSize = light.lightSize;
                settings.enabled = true;
                settings.castShadows = true;
                pendingShadow.push_back({entityId, shadow::ShadowMapType::PointCube, settings});
            }

            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            glm::vec3 position = glm::vec3(worldTransform.worldMatrix[3]);

            GPUPointLight& gpuLight = cpuPointLights[pointCount];
            gpuLight.position = position;
            gpuLight.radius = light.radius;
            gpuLight.color = light.color;
            gpuLight.intensity = light.intensity;
            gpuLight.shadowIndex = -1;

            if (shadowSystem)
            {
                gpuLight.shadowIndex = shadowSystem->getShadowViewIndex(entityId);
            }
            // Default to VSM; assignRTPointSlices() promotes the closest/brightest
            // shadow-casters to an RT mask slice each frame the RT point path is active.
            gpuLight.rtMaskSlice = -1;
            gpuLight.padding[0] = 0;
            gpuLight.padding[1] = 0;
            cpuPointLightEntityIds[pointCount] = entityId;

            ++pointCount;
        }

        if (hitLimit && !warnedPointLimit)
        {
            vfLogWarning("GPULightBufferManager: Exceeded max point lights ({}). Additional lights will be ignored.",
                          LightConstants::MAX_POINT_LIGHTS);
            warnedPointLimit = true;
        }
        else if (!hitLimit && warnedPointLimit)
        {
            warnedPointLimit = false;
        }
    }

    void GPULightBufferManager::appendTransientPointLights()
    {
        for (const auto& light : transientPointLights)
        {
            if (pointCount >= LightConstants::MAX_POINT_LIGHTS)
                break;

            GPUPointLight& gpuLight = cpuPointLights[pointCount];
            gpuLight.position = light.position;
            gpuLight.radius = light.radius;
            gpuLight.color = light.color;
            gpuLight.intensity = light.intensity;
            gpuLight.shadowIndex = -1;
            // Transient lights never cast shadows (shadowIndex = -1) so they are never
            // promoted to an RT slice; keep VSM fallthrough and a sentinel entity id.
            gpuLight.rtMaskSlice = -1;
            gpuLight.padding[0] = 0;
            gpuLight.padding[1] = 0;
            cpuPointLightEntityIds[pointCount] = UINT32_MAX;

            ++pointCount;
        }
    }

    void GPULightBufferManager::collectSpotLights(const std::unordered_set<uint32_t>* visibleLightIds,
                                                    std::vector<PendingShadowReg>& pendingShadow)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::SpotLightComponent, components::WorldTransformComponent>();

        spotCount = 0;
        bool hitLimit = false;

        for (auto entity : view)
        {
            if (!scene::Entity::isEffectivelyActive(registry, entity))
                continue;

            if (visibleLightIds && visibleLightIds->find(static_cast<uint32_t>(entity)) == visibleLightIds->end())
            {
                continue;
            }

            if (spotCount >= LightConstants::MAX_SPOT_LIGHTS)
            {
                hitLimit = true;
                break;
            }

            const auto& light = view.get<components::SpotLightComponent>(entity);

            uint32_t entityId = static_cast<uint32_t>(entity);
            if (shadowSystem && light.castsShadow)
            {
                shadow::ShadowSettings settings{};
                settings.depthBias = shadowSystem->getGlobalDepthBias();
                settings.normalBias = shadowSystem->getGlobalNormalBias();
                settings.farPlane = light.range;
                settings.lightSize = light.lightSize;
                settings.enabled = true;
                settings.castShadows = true;
                pendingShadow.push_back({entityId, shadow::ShadowMapType::Spot2D, settings});
            }

            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            glm::vec3 position = glm::vec3(worldTransform.worldMatrix[3]);
            glm::vec3 direction = glm::normalize(glm::vec3(worldTransform.worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

            GPUSpotLight& gpuLight = cpuSpotLights[spotCount];
            gpuLight.position = position;
            gpuLight.range = light.range;
            gpuLight.direction = direction;
            gpuLight.intensity = light.intensity;
            gpuLight.color = light.color;
            gpuLight.cosInnerAngle = std::cos(glm::radians(light.innerAngle));
            gpuLight.cosOuterAngle = std::cos(glm::radians(light.outerAngle));
            gpuLight.shadowIndex = -1;

            if (shadowSystem)
            {
                gpuLight.shadowIndex = shadowSystem->getShadowViewIndex(entityId);
            }
            // Default to VSM; assignRTSpotSlices() promotes the closest/brightest
            // shadow-casters to an RT mask slice each frame the RT spot path is active.
            gpuLight.rtMaskSlice = -1;
            gpuLight.padding = 0;
            cpuSpotLightEntityIds[spotCount] = entityId;

            ++spotCount;
        }

        if (hitLimit && !warnedSpotLimit)
        {
            vfLogWarning("GPULightBufferManager: Exceeded max spot lights ({}). Additional lights will be ignored.",
                          LightConstants::MAX_SPOT_LIGHTS);
            warnedSpotLimit = true;
        }
        else if (!hitLimit && warnedSpotLimit)
        {
            warnedSpotLimit = false;
        }
    }

    void GPULightBufferManager::processPendingShadowRegistrations()
    {
        for (const auto& reg : pendingDirShadow)
            updateShadowRegistration(reg.entityId, reg.type, reg.settings);
        for (const auto& reg : pendingPointShadow)
            updateShadowRegistration(reg.entityId, reg.type, reg.settings);
        for (const auto& reg : pendingSpotShadow)
            updateShadowRegistration(reg.entityId, reg.type, reg.settings);
    }

    void GPULightBufferManager::cleanupStaleShadowRegistrations()
    {
        if (!shadowSystem || registeredShadowLights.empty())
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<uint32_t> toUnregister;

        for (uint32_t entityId : registeredShadowLights)
        {
            auto entity = static_cast<entt::entity>(entityId);

            bool shouldKeep = false;
            if (registry.valid(entity))
            {
                if (registry.all_of<components::PointLightComponent>(entity))
                    shouldKeep = registry.get<components::PointLightComponent>(entity).castsShadow;
                else if (registry.all_of<components::SpotLightComponent>(entity))
                    shouldKeep = registry.get<components::SpotLightComponent>(entity).castsShadow;
                else if (registry.all_of<components::DirectionalLightComponent>(entity))
                    shouldKeep = scene::Entity::isEffectivelyActive(registry, entity); // sun always casts via VSM clipmap
            }

            if (!shouldKeep)
            {
                toUnregister.push_back(entityId);
            }
        }

        for (uint32_t entityId : toUnregister)
        {
            shadowSystem->unregisterLight(entityId);
            registeredShadowLights.erase(entityId);
        }
    }

    void GPULightBufferManager::updateCountsBuffer()
    {
        if (!countsMapped)
        {
            return;
        }

        GPULightCounts counts{};
        counts.directionalCount = directionalCount;
        counts.pointCount = pointCount;
        counts.spotCount = spotCount;
        counts.shadowIntensity = shadowIntensity;
        counts.rtShadowActive = rtShadowActive ? 1u : 0u;
        counts.rtSpotShadowActive = rtSpotShadowActive ? 1u : 0u;
        counts.rtPointShadowActive = rtPointShadowActive ? 1u : 0u;

        std::memcpy(countsMapped, &counts, sizeof(GPULightCounts));
    }

    void GPULightBufferManager::setShadowIntensity(float intensity)
    {
        if (shadowIntensity != intensity)
        {
            shadowIntensity = intensity;
            updateCountsBuffer();
            needsUpload = true;
        }
    }

    void GPULightBufferManager::setRTShadowActive(bool active)
    {
        if (rtShadowActive != active)
        {
            rtShadowActive = active;
            updateCountsBuffer();
        }
    }

    void GPULightBufferManager::setRTSpotShadowActive(bool active)
    {
        if (rtSpotShadowActive != active)
        {
            rtSpotShadowActive = active;
            updateCountsBuffer();
        }
    }

    void GPULightBufferManager::setRTPointShadowActive(bool active)
    {
        if (rtPointShadowActive != active)
        {
            rtPointShadowActive = active;
            updateCountsBuffer();
        }
    }

    std::vector<uint32_t> GPULightBufferManager::assignRTSpotSlices(const glm::vec3& cameraPos, uint32_t budget)
    {
        return assignRTSlicesGeneric(cpuSpotLights, cpuSpotLightEntityIds, spotCount,
                                     rtSpotSliceLights, rtSpotSliceValid, budget, cameraPos, needsUpload);
    }

    std::vector<uint32_t> GPULightBufferManager::assignRTPointSlices(const glm::vec3& cameraPos, uint32_t budget)
    {
        return assignRTSlicesGeneric(cpuPointLights, cpuPointLightEntityIds, pointCount,
                                     rtPointSliceLights, rtPointSliceValid, budget, cameraPos, needsUpload);
    }

    void GPULightBufferManager::uploadToGPU(vk::CommandBuffer cmd)
    {
        if (!initialized || !needsUpload)
        {
            return;
        }

        std::array<vk::BufferMemoryBarrier, 3> preTransferBarriers{};

        preTransferBarriers[0].srcAccessMask = vk::AccessFlagBits::eShaderRead;
        preTransferBarriers[0].dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        preTransferBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preTransferBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preTransferBarriers[0].buffer = directionalBuffer;
        preTransferBarriers[0].offset = 0;
        preTransferBarriers[0].size = VK_WHOLE_SIZE;

        preTransferBarriers[1].srcAccessMask = vk::AccessFlagBits::eShaderRead;
        preTransferBarriers[1].dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        preTransferBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preTransferBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preTransferBarriers[1].buffer = pointBuffer;
        preTransferBarriers[1].offset = 0;
        preTransferBarriers[1].size = VK_WHOLE_SIZE;

        preTransferBarriers[2].srcAccessMask = vk::AccessFlagBits::eShaderRead;
        preTransferBarriers[2].dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        preTransferBarriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preTransferBarriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preTransferBarriers[2].buffer = spotBuffer;
        preTransferBarriers[2].offset = 0;
        preTransferBarriers[2].size = VK_WHOLE_SIZE;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eFragmentShader |
            vk::PipelineStageFlagBits::eComputeShader |
            vk::PipelineStageFlagBits::eMeshShaderEXT,
            vk::PipelineStageFlagBits::eTransfer,
            {},
            {},
            preTransferBarriers,
            {}
        );

        auto& sf = stagingFrames[currentStagingFrame];

        if (directionalCount > 0)
        {
            size_t copySize = directionalCount * sizeof(GPUDirectionalLight);
            std::memcpy(sf.directionalStagingMapped, cpuDirectionalLights.data(), copySize);

            vk::BufferCopy region{};
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size = copySize;
            cmd.copyBuffer(sf.directionalStagingBuffer, directionalBuffer, region);
        }

        if (pointCount > 0)
        {
            size_t copySize = pointCount * sizeof(GPUPointLight);
            std::memcpy(sf.pointStagingMapped, cpuPointLights.data(), copySize);

            vk::BufferCopy region{};
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size = copySize;
            cmd.copyBuffer(sf.pointStagingBuffer, pointBuffer, region);
        }

        if (spotCount > 0)
        {
            size_t copySize = spotCount * sizeof(GPUSpotLight);
            std::memcpy(sf.spotStagingMapped, cpuSpotLights.data(), copySize);

            vk::BufferCopy region{};
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size = copySize;
            cmd.copyBuffer(sf.spotStagingBuffer, spotBuffer, region);
        }

        std::array<vk::BufferMemoryBarrier, 3> barriers{};

        barriers[0].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barriers[0].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].buffer = directionalBuffer;
        barriers[0].offset = 0;
        barriers[0].size = VK_WHOLE_SIZE;

        barriers[1].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barriers[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].buffer = pointBuffer;
        barriers[1].offset = 0;
        barriers[1].size = VK_WHOLE_SIZE;

        barriers[2].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barriers[2].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[2].buffer = spotBuffer;
        barriers[2].offset = 0;
        barriers[2].size = VK_WHOLE_SIZE;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader |
            vk::PipelineStageFlagBits::eComputeShader |
            vk::PipelineStageFlagBits::eMeshShaderEXT,
            {},
            {},
            barriers,
            {}
        );

        prevDirectionalCount = directionalCount;
        prevPointCount = pointCount;
        prevSpotCount = spotCount;

        prevDirectionalLights.assign(cpuDirectionalLights.begin(),
                                      cpuDirectionalLights.begin() + directionalCount);
        prevPointLights.assign(cpuPointLights.begin(),
                               cpuPointLights.begin() + pointCount);
        prevSpotLights.assign(cpuSpotLights.begin(),
                              cpuSpotLights.begin() + spotCount);

        needsUpload = false;
    }

    bool GPULightBufferManager::detectChanges()
    {
        if (directionalCount != prevDirectionalCount ||
            pointCount != prevPointCount ||
            spotCount != prevSpotCount)
        {
            return true;
        }

        if (directionalCount > 0)
        {
            if (std::memcmp(cpuDirectionalLights.data(), prevDirectionalLights.data(),
                           directionalCount * sizeof(GPUDirectionalLight)) != 0)
            {
                return true;
            }
        }

        if (pointCount > 0)
        {
            if (std::memcmp(cpuPointLights.data(), prevPointLights.data(),
                           pointCount * sizeof(GPUPointLight)) != 0)
            {
                return true;
            }
        }

        if (spotCount > 0)
        {
            if (std::memcmp(cpuSpotLights.data(), prevSpotLights.data(),
                           spotCount * sizeof(GPUSpotLight)) != 0)
            {
                return true;
            }
        }

        return false;
    }

    void GPULightBufferManager::preWarmShadowsForLights(const std::vector<uint32_t>& entityIds)
    {
        if (!shadowSystem)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();

        for (uint32_t entityId : entityIds)
        {
            auto entity = static_cast<entt::entity>(entityId);
            if (!registry.valid(entity))
                continue;

            if (registry.all_of<components::PointLightComponent>(entity))
            {
                const auto& light = registry.get<components::PointLightComponent>(entity);
                if (!light.castsShadow)
                    continue;

                shadow::ShadowSettings settings{};
                settings.depthBias = shadowSystem->getGlobalDepthBias();
                settings.normalBias = shadowSystem->getGlobalNormalBias();
                settings.farPlane = light.radius;
                settings.lightSize = light.lightSize;
                settings.enabled = true;
                settings.castShadows = true;
                updateShadowRegistration(entityId, shadow::ShadowMapType::PointCube, settings);
            }
            else if (registry.all_of<components::SpotLightComponent>(entity))
            {
                const auto& light = registry.get<components::SpotLightComponent>(entity);
                if (!light.castsShadow)
                    continue;

                shadow::ShadowSettings settings{};
                settings.depthBias = shadowSystem->getGlobalDepthBias();
                settings.normalBias = shadowSystem->getGlobalNormalBias();
                settings.farPlane = light.range;
                settings.lightSize = light.lightSize;
                settings.enabled = true;
                settings.castShadows = true;
                updateShadowRegistration(entityId, shadow::ShadowMapType::Spot2D, settings);
            }
        }
    }
}
