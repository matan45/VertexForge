#include "GPULightBufferManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../shadow/ShadowSystem.hpp"
#include "../shadow/ShadowTypes.hpp"
#include "print/Logger.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <cmath>
#include <cstring>

namespace render::lighting
{
    GPULightBufferManager::GPULightBufferManager(core::Device& device)
        : device(device)
    {
    }

    GPULightBufferManager::~GPULightBufferManager()
    {
        cleanup();
    }

    void GPULightBufferManager::init()
    {
        if (initialized)
        {
            return;
        }

        cpuDirectionalLights.resize(LightConstants::MAX_DIRECTIONAL_LIGHTS);
        cpuPointLights.resize(LightConstants::MAX_POINT_LIGHTS);
        cpuSpotLights.resize(LightConstants::MAX_SPOT_LIGHTS);

        loggerInfo("GPULightBufferManager: Initializing with max {} directional, {} point, {} spot lights",
                   LightConstants::MAX_DIRECTIONAL_LIGHTS,
                   LightConstants::MAX_POINT_LIGHTS,
                   LightConstants::MAX_SPOT_LIGHTS);

        createBuffers();
        createDescriptorSetLayout();
        createDescriptorPool();
        allocateDescriptorSet();
        updateDescriptors();

        initialized = true;
        loggerInfo("GPULightBufferManager: Initialized successfully");
    }

    void GPULightBufferManager::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (descriptorPool)
        {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (descriptorSetLayout)
        {
            vkDevice.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }

        destroyBuffers();

        cpuDirectionalLights.clear();
        cpuPointLights.clear();
        cpuSpotLights.clear();

        initialized = false;
        loggerInfo("GPULightBufferManager: Cleaned up");
    }

    void GPULightBufferManager::createBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        {
            vk::DeviceSize bufferSize = LightConstants::MAX_DIRECTIONAL_LIGHTS * sizeof(GPUDirectionalLight);

            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = bufferSize;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, directionalBuffer, directionalMemory);

            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, directionalStagingBuffer, directionalStagingMemory);

            directionalStagingMapped = logicalDevice.mapMemory(directionalStagingMemory, 0, bufferSize, vk::MemoryMapFlags{});
        }

        {
            vk::DeviceSize bufferSize = LightConstants::MAX_POINT_LIGHTS * sizeof(GPUPointLight);

            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = bufferSize;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, pointBuffer, pointMemory);

            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, pointStagingBuffer, pointStagingMemory);

            pointStagingMapped = logicalDevice.mapMemory(pointStagingMemory, 0, bufferSize, vk::MemoryMapFlags{});
        }

        {
            vk::DeviceSize bufferSize = LightConstants::MAX_SPOT_LIGHTS * sizeof(GPUSpotLight);

            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = bufferSize;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, spotBuffer, spotMemory);

            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, spotStagingBuffer, spotStagingMemory);

            spotStagingMapped = logicalDevice.mapMemory(spotStagingMemory, 0, bufferSize, vk::MemoryMapFlags{});
        }

        {
            vk::DeviceSize bufferSize = sizeof(GPULightCounts);

            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = bufferSize;
            request.usage = vk::BufferUsageFlagBits::eUniformBuffer;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, countsBuffer, countsMemory);

            countsMapped = logicalDevice.mapMemory(countsMemory, 0, bufferSize, vk::MemoryMapFlags{});

            GPULightCounts counts{0, 0, 0, 0.5f};
            std::memcpy(countsMapped, &counts, sizeof(GPULightCounts));
        }

        loggerInfo("GPULightBufferManager: Created light buffers");
    }

    void GPULightBufferManager::destroyBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (directionalStagingMapped)
        {
            logicalDevice.unmapMemory(directionalStagingMemory);
            directionalStagingMapped = nullptr;
        }
        if (pointStagingMapped)
        {
            logicalDevice.unmapMemory(pointStagingMemory);
            pointStagingMapped = nullptr;
        }
        if (spotStagingMapped)
        {
            logicalDevice.unmapMemory(spotStagingMemory);
            spotStagingMapped = nullptr;
        }
        if (countsMapped)
        {
            logicalDevice.unmapMemory(countsMemory);
            countsMapped = nullptr;
        }

        core::BufferUtilities::destroyBuffer(logicalDevice, directionalStagingBuffer, directionalStagingMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, directionalBuffer, directionalMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, pointStagingBuffer, pointStagingMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, pointBuffer, pointMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, spotStagingBuffer, spotStagingMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, spotBuffer, spotMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, countsBuffer, countsMemory);
    }

    void GPULightBufferManager::createDescriptorSetLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};

        // Binding 0: Directional lights SSBO
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment |
                                 vk::ShaderStageFlagBits::eCompute |
                                 vk::ShaderStageFlagBits::eMeshEXT;
        bindings[0].pImmutableSamplers = nullptr;

        // Binding 1: Point lights SSBO
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment |
                                 vk::ShaderStageFlagBits::eCompute |
                                 vk::ShaderStageFlagBits::eMeshEXT;
        bindings[1].pImmutableSamplers = nullptr;

        // Binding 2: Spot lights SSBO
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment |
                                 vk::ShaderStageFlagBits::eCompute |
                                 vk::ShaderStageFlagBits::eMeshEXT;
        bindings[2].pImmutableSamplers = nullptr;

        // Binding 3: Light counts UBO
        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eFragment |
                                 vk::ShaderStageFlagBits::eCompute |
                                 vk::ShaderStageFlagBits::eMeshEXT;
        bindings[3].pImmutableSamplers = nullptr;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
        loggerInfo("GPULightBufferManager: Created descriptor set layout");
    }

    void GPULightBufferManager::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[0].descriptorCount = 3;
        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
        loggerInfo("GPULightBufferManager: Created descriptor pool");
    }

    void GPULightBufferManager::allocateDescriptorSet()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        std::vector<vk::DescriptorSet> sets = vkDevice.allocateDescriptorSets(allocInfo);
        descriptorSet = sets[0];

        loggerInfo("GPULightBufferManager: Allocated descriptor set");
    }

    void GPULightBufferManager::updateDescriptors()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorBufferInfo, 4> bufferInfos{};

        bufferInfos[0].buffer = directionalBuffer;
        bufferInfos[0].offset = 0;
        bufferInfos[0].range = VK_WHOLE_SIZE;

        bufferInfos[1].buffer = pointBuffer;
        bufferInfos[1].offset = 0;
        bufferInfos[1].range = VK_WHOLE_SIZE;

        bufferInfos[2].buffer = spotBuffer;
        bufferInfos[2].offset = 0;
        bufferInfos[2].range = VK_WHOLE_SIZE;

        bufferInfos[3].buffer = countsBuffer;
        bufferInfos[3].offset = 0;
        bufferInfos[3].range = sizeof(GPULightCounts);

        std::array<vk::WriteDescriptorSet, 4> descriptorWrites{};

        for (uint32_t i = 0; i < 4; ++i)
        {
            descriptorWrites[i].dstSet = descriptorSet;
            descriptorWrites[i].dstBinding = i;
            descriptorWrites[i].dstArrayElement = 0;
            descriptorWrites[i].descriptorType = (i < 3) ? vk::DescriptorType::eStorageBuffer : vk::DescriptorType::eUniformBuffer;
            descriptorWrites[i].descriptorCount = 1;
            descriptorWrites[i].pBufferInfo = &bufferInfos[i];
        }

        vkDevice.updateDescriptorSets(static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }

    void GPULightBufferManager::updateFromScene()
    {
        // No filtering - collect all lights
        updateFromScene(std::unordered_set<uint32_t>{});
    }

    void GPULightBufferManager::updateFromScene(const std::unordered_set<uint32_t>& visibleLightIds)
    {
        if (!initialized)
        {
            return;
        }

#ifndef NDEBUG
        // Debug validation: check that visibleLightIds contain valid light entities
        // This helps detect sync issues between LightBVH and ECS registry
        if (!visibleLightIds.empty())
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            uint32_t invalidCount = 0;
            uint32_t noLightComponentCount = 0;

            for (uint32_t entityId : visibleLightIds)
            {
                auto entity = static_cast<entt::entity>(entityId);

                if (!registry.valid(entity))
                {
                    ++invalidCount;
                    continue;
                }

                // Check if entity has at least one light component
                bool hasLight = registry.any_of<
                    components::DirectionalLightComponent,
                    components::PointLightComponent,
                    components::SpotLightComponent>(entity);

                if (!hasLight)
                {
                    ++noLightComponentCount;
                }
            }

            if (invalidCount > 0)
            {
                loggerWarning("GPULightBufferManager: {} stale entity IDs in visibleLightIds (BVH may be out of sync)",
                              invalidCount);
            }
            if (noLightComponentCount > 0)
            {
                loggerWarning("GPULightBufferManager: {} entity IDs have no light component (BVH contains non-light entities)",
                              noLightComponentCount);
            }
        }
#endif

        // If the set is empty, collect all lights (no filtering)
        // Otherwise, only collect lights that are in the visible set
        const std::unordered_set<uint32_t>* filterPtr = visibleLightIds.empty() ? nullptr : &visibleLightIds;

        collectDirectionalLights(filterPtr);
        collectPointLights(filterPtr);
        collectSpotLights(filterPtr);

        // Clean up shadow registrations for lights whose components were removed
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
            // Skip inactive entities
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
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            // Extract forward direction from world matrix (handles parented entities correctly)
            // Transform local forward (0, 0, -1) by the world matrix rotation
            glm::vec3 direction = glm::normalize(glm::vec3(worldTransform.worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

            GPUDirectionalLight& gpuLight = cpuDirectionalLights[directionalCount];
            gpuLight.direction = direction;
            gpuLight.intensity = light.intensity;
            gpuLight.color = light.color;

            // Handle shadow registration based on global shadow settings
            uint32_t entityId = static_cast<uint32_t>(entity);
            gpuLight.shadowIndex = -1;  // Default: no shadow

            if (shadowSystem)
            {
                bool isRegistered = registeredShadowLights.contains(entityId);
                bool shadowsEnabled = shadowSystem->isShadowsEnabled();

                if (shadowsEnabled)
                {
                    // Register or update light for shadow casting (CSM for directional lights)
                    // Use global settings from shadow system
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
                    // Unregister light from shadow casting when shadows disabled globally
                    shadowSystem->unregisterLight(entityId);
                    registeredShadowLights.erase(entityId);
                }

                // Query shadow index
                gpuLight.shadowIndex = shadowSystem->getShadowViewIndex(entityId);
            }

            ++directionalCount;
        }

        // One-time warning when limit is exceeded (resets when count drops below limit)
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
            // Skip inactive entities
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
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            glm::vec3 position = glm::vec3(worldTransform.worldMatrix[3]);

            GPUPointLight& gpuLight = cpuPointLights[pointCount];
            gpuLight.position = position;
            gpuLight.radius = light.radius;
            gpuLight.color = light.color;
            gpuLight.intensity = light.intensity;

            // Handle shadow registration based on global shadow settings
            uint32_t entityId = static_cast<uint32_t>(entity);
            gpuLight.shadowIndex = -1;  // Default: no shadow

            if (shadowSystem)
            {
                bool isRegistered = registeredShadowLights.contains(entityId);
                bool shadowsEnabled = shadowSystem->isShadowsEnabled();

                if (shadowsEnabled)
                {
                    // Register or update light for shadow casting (cube map for point lights)
                    // Use global bias from shadow system settings
                    shadow::ShadowSettings settings{};
                    settings.depthBias = shadowSystem->getGlobalDepthBias();
                    settings.normalBias = shadowSystem->getGlobalNormalBias();
                    settings.farPlane = light.radius;  // Use light radius as far plane
                    settings.enabled = true;
                    settings.castShadows = true;

                    if (shadowSystem->registerLight(entityId, shadow::ShadowMapType::PointCube, settings))
                    {
                        registeredShadowLights.insert(entityId);
                    }
                }
                else if (!shadowsEnabled && isRegistered)
                {
                    // Unregister light from shadow casting when shadows disabled globally
                    shadowSystem->unregisterLight(entityId);
                    registeredShadowLights.erase(entityId);
                }

                // Query shadow index
                gpuLight.shadowIndex = shadowSystem->getShadowViewIndex(entityId);
            }
            gpuLight.padding[0] = 0;
            gpuLight.padding[1] = 0;
            gpuLight.padding[2] = 0;

            ++pointCount;
        }

        // One-time warning when limit is exceeded (resets when count drops below limit)
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
            // Skip inactive entities
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
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            glm::vec3 position = glm::vec3(worldTransform.worldMatrix[3]);

            // Extract forward direction from world matrix (handles parented entities correctly)
            // Transform local forward (0, 0, -1) by the world matrix rotation
            glm::vec3 direction = glm::normalize(glm::vec3(worldTransform.worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

            GPUSpotLight& gpuLight = cpuSpotLights[spotCount];
            gpuLight.position = position;
            gpuLight.range = light.range;
            gpuLight.direction = direction;
            gpuLight.intensity = light.intensity;
            gpuLight.color = light.color;
            gpuLight.cosInnerAngle = std::cos(glm::radians(light.innerAngle));
            gpuLight.cosOuterAngle = std::cos(glm::radians(light.outerAngle));

            // Handle shadow registration based on global shadow settings
            uint32_t entityId = static_cast<uint32_t>(entity);
            gpuLight.shadowIndex = -1;  // Default: no shadow

            if (shadowSystem)
            {
                bool isRegistered = registeredShadowLights.contains(entityId);
                bool shadowsEnabled = shadowSystem->isShadowsEnabled();

                if (shadowsEnabled)
                {
                    // Register or update light for shadow casting
                    // Use global bias from shadow system settings
                    shadow::ShadowSettings settings{};
                    settings.depthBias = shadowSystem->getGlobalDepthBias();
                    settings.normalBias = shadowSystem->getGlobalNormalBias();
                    settings.farPlane = light.range;  // Use light range as far plane
                    settings.enabled = true;
                    settings.castShadows = true;

                    if (shadowSystem->registerLight(entityId, shadow::ShadowMapType::Spot2D, settings))
                    {
                        registeredShadowLights.insert(entityId);
                    }
                }
                else if (!shadowsEnabled && isRegistered)
                {
                    // Unregister light from shadow casting when shadows disabled globally
                    shadowSystem->unregisterLight(entityId);
                    registeredShadowLights.erase(entityId);
                }

                // Query shadow index
                gpuLight.shadowIndex = shadowSystem->getShadowViewIndex(entityId);
            }
            gpuLight.padding[0] = 0;
            gpuLight.padding[1] = 0;

            ++spotCount;
        }

        // One-time warning when limit is exceeded (resets when count drops below limit)
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

        // Collect entity IDs that need to be unregistered
        std::vector<uint32_t> toUnregister;

        for (uint32_t entityId : registeredShadowLights)
        {
            auto entity = static_cast<entt::entity>(entityId);

            // Check if entity is still valid and has a light component
            bool shouldKeep = registry.valid(entity) &&
                              registry.any_of<components::DirectionalLightComponent,
                                              components::PointLightComponent,
                                              components::SpotLightComponent>(entity);

            if (!shouldKeep)
            {
                toUnregister.push_back(entityId);
            }
        }

        // Unregister stale lights from shadow system
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

        // Barrier: Wait for previous frame's shader reads to complete before writing
        // This prevents a race condition where we write to buffers still being read
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
