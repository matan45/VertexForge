#include "GPULightBufferManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../shadow/ShadowSystem.hpp"
#include "../shadow/ShadowTypes.hpp"
#include "print/Logger.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "components/LightTextComponents.hpp"
#include <cmath>

namespace render::lighting
{
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

        collectDirectionalLights(filterPtr);
        collectPointLights(filterPtr);
        collectSpotLights(filterPtr);
        cleanupStaleShadowRegistrations();

        if (detectChanges())
        {
            updateCountsBuffer();
            needsUpload = true;
        }
    }

    void GPULightBufferManager::collectDirectionalLights(const std::unordered_set<uint32_t>* visibleLightIds)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::DirectionalLightComponent, components::WorldTransformComponent>();

        directionalCount = 0;
        bool hitLimit = false;

        for (auto entity : view)
        {
            if (auto* nameComp = registry.try_get<components::NameComponent>(entity))
            {
                if (!nameComp->isActive)
                    continue;
            }

            if (visibleLightIds && visibleLightIds->find(static_cast<uint32_t>(entity)) == visibleLightIds->end())
            {
                continue;
            }

            if (directionalCount >= LightConstants::MAX_DIRECTIONAL_LIGHTS)
            {
                hitLimit = true;
                break;
            }

            const auto& light = view.get<components::DirectionalLightComponent>(entity);
            if (auto* transform = registry.try_get<components::TransformComponent>(entity))
            {
                if (transform->isStatic) continue;
            }

            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            glm::vec3 direction = glm::normalize(glm::vec3(worldTransform.worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

            GPUDirectionalLight& gpuLight = cpuDirectionalLights[directionalCount];
            gpuLight.direction = direction;
            gpuLight.intensity = light.intensity;
            gpuLight.color = light.color;

            uint32_t entityId = static_cast<uint32_t>(entity);
            gpuLight.shadowIndex = -1;

            if (shadowSystem)
            {
                bool isRegistered = registeredShadowLights.contains(entityId);
                bool shadowsEnabled = shadowSystem->isShadowsEnabled();

                if (shadowsEnabled)
                {
                    shadow::ShadowSettings settings{};
                    settings.depthBias = shadowSystem->getGlobalDepthBias();
                    settings.normalBias = shadowSystem->getGlobalNormalBias();
                    settings.cascadeCount = shadowSystem->getGlobalCascadeCount();
                    settings.enabled = true;
                    settings.castShadows = true;

                    if (shadowSystem->registerLight(entityId, shadow::ShadowMapType::DirectionalCSM, settings))
                    {
                        registeredShadowLights.insert(entityId);
                    }
                }
                else if (!shadowsEnabled && isRegistered)
                {
                    shadowSystem->unregisterLight(entityId);
                    registeredShadowLights.erase(entityId);
                }

                gpuLight.shadowIndex = shadowSystem->getShadowViewIndex(entityId);
            }

            ++directionalCount;
        }

        if (hitLimit && !warnedDirectionalLimit)
        {
            loggerWarning("GPULightBufferManager: Exceeded max directional lights ({}). Additional lights will be ignored.",
                          LightConstants::MAX_DIRECTIONAL_LIGHTS);
            warnedDirectionalLimit = true;
        }
        else if (!hitLimit && warnedDirectionalLimit)
        {
            warnedDirectionalLimit = false;
        }
    }

    void GPULightBufferManager::collectPointLights(const std::unordered_set<uint32_t>* visibleLightIds)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::PointLightComponent, components::WorldTransformComponent>();

        pointCount = 0;
        bool hitLimit = false;

        for (auto entity : view)
        {
            if (auto* nameComp = registry.try_get<components::NameComponent>(entity))
            {
                if (!nameComp->isActive)
                    continue;
            }

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
            if (auto* transform = registry.try_get<components::TransformComponent>(entity))
            {
                if (transform->isStatic) continue;
            }

            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            glm::vec3 position = glm::vec3(worldTransform.worldMatrix[3]);

            GPUPointLight& gpuLight = cpuPointLights[pointCount];
            gpuLight.position = position;
            gpuLight.radius = light.radius;
            gpuLight.color = light.color;
            gpuLight.intensity = light.intensity;

            uint32_t entityId = static_cast<uint32_t>(entity);
            gpuLight.shadowIndex = -1;

            if (shadowSystem)
            {
                bool isRegistered = registeredShadowLights.contains(entityId);
                bool shadowsEnabled = shadowSystem->isShadowsEnabled();

                if (shadowsEnabled)
                {
                    shadow::ShadowSettings settings{};
                    settings.depthBias = shadowSystem->getGlobalDepthBias();
                    settings.normalBias = shadowSystem->getGlobalNormalBias();
                    settings.farPlane = light.radius;
                    settings.enabled = true;
                    settings.castShadows = true;

                    if (shadowSystem->registerLight(entityId, shadow::ShadowMapType::PointCube, settings))
                    {
                        registeredShadowLights.insert(entityId);
                    }
                }
                else if (!shadowsEnabled && isRegistered)
                {
                    shadowSystem->unregisterLight(entityId);
                    registeredShadowLights.erase(entityId);
                }

                gpuLight.shadowIndex = shadowSystem->getShadowViewIndex(entityId);
            }
            gpuLight.padding[0] = 0;
            gpuLight.padding[1] = 0;
            gpuLight.padding[2] = 0;

            ++pointCount;
        }

        if (hitLimit && !warnedPointLimit)
        {
            loggerWarning("GPULightBufferManager: Exceeded max point lights ({}). Additional lights will be ignored.",
                          LightConstants::MAX_POINT_LIGHTS);
            warnedPointLimit = true;
        }
        else if (!hitLimit && warnedPointLimit)
        {
            warnedPointLimit = false;
        }
    }

    void GPULightBufferManager::collectSpotLights(const std::unordered_set<uint32_t>* visibleLightIds)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::SpotLightComponent, components::WorldTransformComponent>();

        spotCount = 0;
        bool hitLimit = false;

        for (auto entity : view)
        {
            if (auto* nameComp = registry.try_get<components::NameComponent>(entity))
            {
                if (!nameComp->isActive)
                    continue;
            }

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
            if (auto* transform = registry.try_get<components::TransformComponent>(entity))
            {
                if (transform->isStatic) continue;
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

            uint32_t entityId = static_cast<uint32_t>(entity);
            gpuLight.shadowIndex = -1;

            if (shadowSystem)
            {
                bool isRegistered = registeredShadowLights.contains(entityId);
                bool shadowsEnabled = shadowSystem->isShadowsEnabled();

                if (shadowsEnabled)
                {
                    shadow::ShadowSettings settings{};
                    settings.depthBias = shadowSystem->getGlobalDepthBias();
                    settings.normalBias = shadowSystem->getGlobalNormalBias();
                    settings.farPlane = light.range;
                    settings.enabled = true;
                    settings.castShadows = true;

                    if (shadowSystem->registerLight(entityId, shadow::ShadowMapType::Spot2D, settings))
                    {
                        registeredShadowLights.insert(entityId);
                    }
                }
                else if (!shadowsEnabled && isRegistered)
                {
                    shadowSystem->unregisterLight(entityId);
                    registeredShadowLights.erase(entityId);
                }

                gpuLight.shadowIndex = shadowSystem->getShadowViewIndex(entityId);
            }
            gpuLight.padding[0] = 0;
            gpuLight.padding[1] = 0;

            ++spotCount;
        }

        if (hitLimit && !warnedSpotLimit)
        {
            loggerWarning("GPULightBufferManager: Exceeded max spot lights ({}). Additional lights will be ignored.",
                          LightConstants::MAX_SPOT_LIGHTS);
            warnedSpotLimit = true;
        }
        else if (!hitLimit && warnedSpotLimit)
        {
            warnedSpotLimit = false;
        }
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

            bool shouldKeep = registry.valid(entity) &&
                              registry.any_of<components::DirectionalLightComponent,
                                              components::PointLightComponent,
                                              components::SpotLightComponent>(entity);

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

        if (directionalCount > 0)
        {
            size_t copySize = directionalCount * sizeof(GPUDirectionalLight);
            std::memcpy(directionalStagingMapped, cpuDirectionalLights.data(), copySize);

            vk::BufferCopy region{};
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size = copySize;
            cmd.copyBuffer(directionalStagingBuffer, directionalBuffer, region);
        }

        if (pointCount > 0)
        {
            size_t copySize = pointCount * sizeof(GPUPointLight);
            std::memcpy(pointStagingMapped, cpuPointLights.data(), copySize);

            vk::BufferCopy region{};
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size = copySize;
            cmd.copyBuffer(pointStagingBuffer, pointBuffer, region);
        }

        if (spotCount > 0)
        {
            size_t copySize = spotCount * sizeof(GPUSpotLight);
            std::memcpy(spotStagingMapped, cpuSpotLights.data(), copySize);

            vk::BufferCopy region{};
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size = copySize;
            cmd.copyBuffer(spotStagingBuffer, spotBuffer, region);
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
}
