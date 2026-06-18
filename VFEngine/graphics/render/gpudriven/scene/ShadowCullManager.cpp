#include "ShadowCullManager.hpp"
#include "GPUCullLODPipeline.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "math/Frustum.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <array>
#include <cstring>

namespace render::gpudriven
{
    ShadowCullManager::ShadowCullManager(core::Device& device)
        : device(device)
    {
    }

    ShadowCullManager::~ShadowCullManager()
    {
        cleanup();
    }

    bool ShadowCullManager::init(GPUCullLODPipeline* cull, vk::DescriptorSetLayout perDrawLayout)
    {
        if (initialized) return true;
        if (!cull || !perDrawLayout)
        {
            vfLogError("ShadowCullManager: init requires a cull pipeline and per-draw layout");
            return false;
        }
        cullPipeline = cull;
        perDrawDataLayout = perDrawLayout;

        if (!createBuffers())
            return false;

        vk::Device vkDevice = device.getLogicalDevice();

        // Pool for the per-view shadow cull sets (same 7-binding layout as the main cull set:
        // 5 storage + 1 uniform + 1 combined-image-sampler), one set per region.
        {
            std::array<vk::DescriptorPoolSize, 3> sizes{};
            sizes[0].type = vk::DescriptorType::eStorageBuffer;
            sizes[0].descriptorCount = 5 * SHADOW_CULL_MAX_VIEWS;
            sizes[1].type = vk::DescriptorType::eUniformBuffer;
            sizes[1].descriptorCount = 1 * SHADOW_CULL_MAX_VIEWS;
            sizes[2].type = vk::DescriptorType::eCombinedImageSampler;
            sizes[2].descriptorCount = 1 * SHADOW_CULL_MAX_VIEWS;
            cullSetPool = core::PipelineUtilities::createUpdateAfterBindPool(
                vkDevice, SHADOW_CULL_MAX_VIEWS, sizes.data(), static_cast<uint32_t>(sizes.size()));
        }

        cullSets.resize(SHADOW_CULL_MAX_VIEWS);
        for (uint32_t i = 0; i < SHADOW_CULL_MAX_VIEWS; ++i)
        {
            cullSets[i] = cullPipeline->allocateShadowDescriptorSet(
                cullSetPool, cameraBuffer, i * cameraStride,
                drawCommandBuffer, perDrawDataBuffer, drawCountBuffer);
        }

        // Pool + set for the shadow pass set-0 variant (b0 perDrawData -> our shadow buffer).
        {
            vk::DescriptorPoolSize size{};
            size.type = vk::DescriptorType::eStorageBuffer;
            size.descriptorCount = 3;
            perDrawSetPool = core::PipelineUtilities::createUpdateAfterBindPool(vkDevice, 1, &size, 1);

            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = perDrawSetPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &perDrawDataLayout;
            shadowPerDrawSet = vkDevice.allocateDescriptorSets(allocInfo)[0];
        }

        initialized = true;
        vfLogInfo("ShadowCullManager: initialized ({} views x {} draws, {:.1f} MB)",
                  SHADOW_CULL_MAX_VIEWS, SHADOW_CULL_DRAWS_PER_VIEW,
                  (SHADOW_CULL_MAX_VIEWS * SHADOW_CULL_DRAWS_PER_VIEW *
                   (sizeof(MeshTasksIndirectCommand) + sizeof(PerDrawData))) / (1024.0f * 1024.0f));
        return true;
    }

    bool ShadowCullManager::createBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        const vk::DeviceSize totalDraws =
            static_cast<vk::DeviceSize>(SHADOW_CULL_MAX_VIEWS) * SHADOW_CULL_DRAWS_PER_VIEW;

        try
        {
            {
                core::BufferInfoRequest request(logicalDevice, physicalDevice);
                request.size = totalDraws * sizeof(MeshTasksIndirectCommand);
                request.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                    vk::BufferUsageFlagBits::eIndirectBuffer |
                    vk::BufferUsageFlagBits::eTransferDst;
                request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::BufferUtilities::createBuffer(request, drawCommandBuffer, drawCommandAllocation, device.getMemoryManager());
            }
            {
                core::BufferInfoRequest request(logicalDevice, physicalDevice);
                request.size = totalDraws * sizeof(PerDrawData);
                request.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                    vk::BufferUsageFlagBits::eTransferDst;
                request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::BufferUtilities::createBuffer(request, perDrawDataBuffer, perDrawDataAllocation, device.getMemoryManager());
            }
            {
                core::BufferInfoRequest request(logicalDevice, physicalDevice);
                request.size = SHADOW_CULL_MAX_VIEWS * sizeof(uint32_t);
                request.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                    vk::BufferUsageFlagBits::eIndirectBuffer |
                    vk::BufferUsageFlagBits::eTransferDst;
                request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::BufferUtilities::createBuffer(request, drawCountBuffer, drawCountAllocation, device.getMemoryManager());
            }
            {
                vk::DeviceSize align = physicalDevice.getProperties().limits.minUniformBufferOffsetAlignment;
                if (align == 0) align = 1;
                cameraStride = ((sizeof(GPUCameraData) + align - 1) / align) * align;

                core::BufferInfoRequest request(logicalDevice, physicalDevice);
                request.size = SHADOW_CULL_MAX_VIEWS * cameraStride;
                request.usage = vk::BufferUsageFlagBits::eUniformBuffer;
                request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                    vk::MemoryPropertyFlagBits::eHostCoherent;
                core::BufferUtilities::createBuffer(request, cameraBuffer, cameraAllocation, device.getMemoryManager());
                cameraMapped = cameraAllocation.mappedPtr;
            }
            return true;
        }
        catch (const vk::SystemError& e)
        {
            vfLogError("ShadowCullManager: buffer creation failed - {}", e.what());
            destroyBuffers();
            return false;
        }
    }

    void ShadowCullManager::destroyBuffers()
    {
        vk::Device vkDevice = device.getLogicalDevice();
        if (drawCommandBuffer) core::BufferUtilities::destroyBuffer(vkDevice, drawCommandBuffer, drawCommandAllocation, device.getMemoryManager());
        if (perDrawDataBuffer) core::BufferUtilities::destroyBuffer(vkDevice, perDrawDataBuffer, perDrawDataAllocation, device.getMemoryManager());
        if (drawCountBuffer)   core::BufferUtilities::destroyBuffer(vkDevice, drawCountBuffer, drawCountAllocation, device.getMemoryManager());
        if (cameraBuffer)      core::BufferUtilities::destroyBuffer(vkDevice, cameraBuffer, cameraAllocation, device.getMemoryManager());
        drawCommandBuffer = nullptr;
        perDrawDataBuffer = nullptr;
        drawCountBuffer = nullptr;
        cameraBuffer = nullptr;
        cameraMapped = nullptr;
    }

    void ShadowCullManager::cleanup()
    {
        if (!initialized) return;
        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        // Untrack the per-view cull sets from the pipeline before freeing their pool.
        if (cullPipeline)
            for (auto set : cullSets)
                cullPipeline->releaseExternalDescriptorSet(set);
        cullSets.clear();

        if (cullSetPool)   { vkDevice.destroyDescriptorPool(cullSetPool); cullSetPool = nullptr; }
        if (perDrawSetPool){ vkDevice.destroyDescriptorPool(perDrawSetPool); perDrawSetPool = nullptr; }
        destroyBuffers();
        initialized = false;
    }

    void ShadowCullManager::updateSceneBuffers(vk::Buffer instanceTransformBuffer, vk::Buffer objectBuffer)
    {
        if (!initialized || !shadowPerDrawSet) return;

        std::array<vk::DescriptorBufferInfo, 3> infos{};
        infos[0].buffer = perDrawDataBuffer;       infos[0].offset = 0; infos[0].range = VK_WHOLE_SIZE;
        infos[1].buffer = instanceTransformBuffer; infos[1].offset = 0; infos[1].range = VK_WHOLE_SIZE;
        infos[2].buffer = objectBuffer;            infos[2].offset = 0; infos[2].range = VK_WHOLE_SIZE;

        std::array<vk::WriteDescriptorSet, 3> writes{};
        for (uint32_t i = 0; i < 3; ++i)
        {
            if (!infos[i].buffer) return; // scene buffers not ready yet
            writes[i].dstSet = shadowPerDrawSet;
            writes[i].dstBinding = i;
            writes[i].dstArrayElement = 0;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[i].pBufferInfo = &infos[i];
        }
        device.getLogicalDevice().updateDescriptorSets(writes, {});
    }

    uint32_t ShadowCullManager::beginFrame(const std::vector<glm::mat4>& viewProjections, uint32_t objectCount)
    {
        if (!initialized) { activeViewCount = 0; return 0; }

        activeViewCount = std::min<uint32_t>(static_cast<uint32_t>(viewProjections.size()), SHADOW_CULL_MAX_VIEWS);
        if (viewProjections.size() > SHADOW_CULL_MAX_VIEWS)
        {
            vfLogWarning("ShadowCullManager: {} shadow views exceed cap {}; extra views use the legacy buffer",
                         viewProjections.size(), SHADOW_CULL_MAX_VIEWS);
        }

        for (uint32_t i = 0; i < activeViewCount; ++i)
        {
            GPUCameraData cam{};
            cam.viewProjection = viewProjections[i];
            cam.prevViewProjection = viewProjections[i];
            extractFrustumPlanes(viewProjections[i], cam.frustumPlanes);
            cam.objectCount = objectCount;
            cam.hiZMipLevels = 0;                          // no occlusion (camera Hi-Z is not the light's)
            cam.enableFrustumCulling = 1;
            cam.enableOcclusionCulling = 0;
            cam.enableLODSelection = 0;                    // LOD_SELECTION_DISABLED — shadows use LOD 0
            cam.enableDistanceCulling = 0;

            auto* dst = static_cast<char*>(cameraMapped) + static_cast<size_t>(i) * cameraStride;
            std::memcpy(dst, &cam, sizeof(GPUCameraData));
        }
        return activeViewCount;
    }

    void ShadowCullManager::recordCull(vk::CommandBuffer cmd, uint32_t objectCount)
    {
        if (!initialized || activeViewCount == 0 || objectCount == 0) return;

        // Reset every view's counter to 0.
        cmd.fillBuffer(drawCountBuffer, 0, SHADOW_CULL_MAX_VIEWS * sizeof(uint32_t), 0u);
        {
            vk::BufferMemoryBarrier b{};
            b.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            b.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.buffer = drawCountBuffer;
            b.offset = 0;
            b.size = VK_WHOLE_SIZE;
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                vk::PipelineStageFlagBits::eComputeShader, {}, 0, nullptr, 1, &b, 0, nullptr);
        }

        for (uint32_t slot = 0; slot < activeViewCount; ++slot)
        {
            ShadowCullPushConstants pc{};
            pc.viewBase = viewBase(slot);
            pc.capacity = SHADOW_CULL_DRAWS_PER_VIEW;
            pc.countIndex = slot;
            cullPipeline->dispatchShadowWithSet(cmd, objectCount, cullSets[slot], pc);
        }

        // Shadow cull output -> indirect draw (cmd + count) + task/mesh shader read (perDraw).
        std::array<vk::BufferMemoryBarrier, 3> barriers{};
        barriers[0].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[0].dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
        barriers[0].buffer = drawCommandBuffer;
        barriers[1].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[1].dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
        barriers[1].buffer = drawCountBuffer;
        barriers[2].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[2].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barriers[2].buffer = perDrawDataBuffer;
        for (auto& b : barriers)
        {
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.offset = 0;
            b.size = VK_WHOLE_SIZE;
        }
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eTaskShaderEXT,
            {}, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data(), 0, nullptr);
    }

    void ShadowCullManager::extractFrustumPlanes(const glm::mat4& viewProjection, glm::vec4 planes[6])
    {
        math::extractFrustumPlanes(viewProjection, planes);
    }
}
