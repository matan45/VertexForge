#include "VFXRibbonGPUPipeline.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Texture.hpp"
#include "../../../core/DeferredDeletionQueue.hpp"
#include "../bindless/VFXBindlessTextures.hpp"
#include "print/Log.hpp"
#include "../compute/GPUVFXTypes.hpp"
#include "vfx/VFXSortOrder.hpp"
#include <filesystem>

namespace render::vfx
{
    void VFXRibbonGPUPipeline::updateCameraUBO(
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPos,
        float time,
        float nearPlane,
        float farPlane) const
    {
        if (!cameraUBOMapped)
        {
            return;
        }

        GPUVFXCameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;
        ubo.time = time;
        ubo.nearPlane = nearPlane;
        ubo.farPlane = farPlane;

        std::memcpy(cameraUBOMapped, &ubo, sizeof(ubo));
    }

    void VFXRibbonGPUPipeline::setSceneDepthImageView(vk::ImageView depthView)
    {
        if (sceneDepthImageView != depthView)
        {
            sceneDepthImageView = depthView;
            descriptorsNeedUpdate = true;
        }
    }

    void VFXRibbonGPUPipeline::updateParticleBuffer(vk::Buffer particleBuffer, vk::DeviceSize particleBufferSize)
    {
        if (particleBuffer != cachedParticleBuffer || particleBufferSize != cachedParticleBufferSize)
        {
            cachedParticleBuffer = particleBuffer;
            cachedParticleBufferSize = particleBufferSize;
            descriptorsNeedUpdate = true;
        }
    }

    void VFXRibbonGPUPipeline::updateConfigBuffer(vk::Buffer configBuffer, vk::DeviceSize configBufferSize)
    {
        if (configBuffer != cachedConfigBuffer || configBufferSize != cachedConfigBufferSize)
        {
            cachedConfigBuffer = configBuffer;
            cachedConfigBufferSize = configBufferSize;
            descriptorsNeedUpdate = true;
        }
    }

    void VFXRibbonGPUPipeline::updateRibbonBuffers(vk::Buffer ringBuffer, vk::DeviceSize ringBufferSize,
                                                      vk::Buffer headBuffer, vk::DeviceSize headBufferSize)
    {
        if (ringBuffer != cachedRibbonRingBuffer || ringBufferSize != cachedRibbonRingBufferSize ||
            headBuffer != cachedRibbonHeadBuffer || headBufferSize != cachedRibbonHeadBufferSize)
        {
            cachedRibbonRingBuffer = ringBuffer;
            cachedRibbonRingBufferSize = ringBufferSize;
            cachedRibbonHeadBuffer = headBuffer;
            cachedRibbonHeadBufferSize = headBufferSize;
            descriptorsNeedUpdate = true;
        }
    }

    void VFXRibbonGPUPipeline::updateLutBuffer(vk::Buffer lutBuffer, vk::DeviceSize lutBufferSize)
    {
        if (lutBuffer != cachedLutBuffer || lutBufferSize != cachedLutBufferSize)
        {
            cachedLutBuffer = lutBuffer;
            cachedLutBufferSize = lutBufferSize;
            descriptorsNeedUpdate = true;
        }
    }

    void VFXRibbonGPUPipeline::writeDescriptors() const
    {
        if (!descriptorsNeedUpdate || !cachedParticleBuffer || !cachedConfigBuffer ||
            !cachedRibbonRingBuffer || !cachedRibbonHeadBuffer || !cachedLutBuffer)
        {
            return;
        }

        writeDescriptorSet(defaultDescriptorSet);

        descriptorsNeedUpdate = false;
    }

    void VFXRibbonGPUPipeline::writeDescriptorSet(vk::DescriptorSet dstSet) const
    {
        auto vkDevice = device.getLogicalDevice();

        vk::DescriptorBufferInfo cameraInfo{};
        cameraInfo.buffer = cameraUBO;
        cameraInfo.offset = 0;
        cameraInfo.range = sizeof(GPUVFXCameraUBO);

        vk::DescriptorBufferInfo particleInfo{};
        particleInfo.buffer = cachedParticleBuffer;
        particleInfo.offset = 0;
        particleInfo.range = cachedParticleBufferSize;

        vk::DescriptorBufferInfo configInfo{};
        configInfo.buffer = cachedConfigBuffer;
        configInfo.offset = 0;
        configInfo.range = cachedConfigBufferSize;

        vk::DescriptorImageInfo depthInfo{};
        depthInfo.imageView = sceneDepthImageView ? sceneDepthImageView : defaultTextureImageView;
        depthInfo.imageLayout = sceneDepthImageView
            ? vk::ImageLayout::eDepthStencilReadOnlyOptimal
            : vk::ImageLayout::eShaderReadOnlyOptimal;
        depthInfo.sampler = depthSampler ? depthSampler : textureSampler;

        vk::DescriptorBufferInfo ringInfo{};
        ringInfo.buffer = cachedRibbonRingBuffer;
        ringInfo.offset = 0;
        ringInfo.range = cachedRibbonRingBufferSize;

        vk::DescriptorBufferInfo headInfo{};
        headInfo.buffer = cachedRibbonHeadBuffer;
        headInfo.offset = 0;
        headInfo.range = cachedRibbonHeadBufferSize;

        vk::DescriptorBufferInfo lutInfo{};
        lutInfo.buffer = cachedLutBuffer;
        lutInfo.offset = 0;
        lutInfo.range = cachedLutBufferSize;

        vk::DescriptorBufferInfo renderDataInfo{};
        renderDataInfo.buffer = renderDataBuffer;
        renderDataInfo.offset = 0;
        renderDataInfo.range = sizeof(VFXEmitterRenderData) * GPUVFXConstants::MAX_EMITTERS;

        // VK-1481: binding 1 (per-emitter texture) is gone; the texture lives in the bindless set.
        // The ribbon-specific ring (5) / head (6) / LUT (7) SSBO writes are preserved. Phase 2 adds
        // the per-emitter render-data SSBO at binding 8.
        std::array<vk::WriteDescriptorSet, 8> writes{};

        writes[0].dstSet = dstSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[0].pBufferInfo = &cameraInfo;

        writes[1].dstSet = dstSet;
        writes[1].dstBinding = 2;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[1].pBufferInfo = &particleInfo;

        writes[2].dstSet = dstSet;
        writes[2].dstBinding = 3;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[2].pBufferInfo = &configInfo;

        writes[3].dstSet = dstSet;
        writes[3].dstBinding = 4;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[3].pImageInfo = &depthInfo;

        writes[4].dstSet = dstSet;
        writes[4].dstBinding = 5;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[4].pBufferInfo = &ringInfo;

        writes[5].dstSet = dstSet;
        writes[5].dstBinding = 6;
        writes[5].descriptorCount = 1;
        writes[5].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[5].pBufferInfo = &headInfo;

        writes[6].dstSet = dstSet;
        writes[6].dstBinding = 7;
        writes[6].descriptorCount = 1;
        writes[6].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[6].pBufferInfo = &lutInfo;

        // VK-1481 Phase 2: per-emitter render-data SSBO
        writes[7].dstSet = dstSet;
        writes[7].dstBinding = 8;
        writes[7].descriptorCount = 1;
        writes[7].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[7].pBufferInfo = &renderDataInfo;

        vkDevice.updateDescriptorSets(writes, {});
    }

    void VFXRibbonGPUPipeline::setEmitterTexture(uint32_t emitterIndex, const std::string& texturePath)
    {
        auto& config = emitterConfigs[emitterIndex];
        const std::string oldPath = config.texturePath;

        if (oldPath == texturePath)
        {
            return;
        }

        config.texturePath = texturePath;

        // VK-1481: the shared bindless table handles dedup, refcount, cap, and (deferred) teardown.
        if (!oldPath.empty() && bindless)
        {
            bindless->release(oldPath, /*srgb=*/true);
        }

        if (texturePath.empty() || !bindless)
        {
            config.textureIndex = bindless ? bindless->defaultWhiteIndex() : 0u;
            return;
        }

        const uint32_t idx = bindless->acquire(texturePath, /*srgb=*/true);
        config.textureIndex = idx;
        if (idx == bindless->defaultWhiteIndex())
        {
            // Missing / table-full / load error (already logged): drop the path so a later
            // release() cannot over-decrement a reference we never took.
            config.texturePath.clear();
        }
    }

    void VFXRibbonGPUPipeline::setEmitterRenderingConfig(uint32_t emitterIndex,
                                                           float alphaClipThreshold, ::vfx::VFXBlendMode blendMode,
                                                           const glm::vec3& glowColor, int32_t sortOrder)
    {
        emitterConfigs[emitterIndex].alphaClipThreshold = alphaClipThreshold;
        emitterConfigs[emitterIndex].blendMode = ::vfx::blendModeToGpuValue(blendMode);
        emitterConfigs[emitterIndex].glowColor = glowColor;
        emitterConfigs[emitterIndex].sortOrder = sortOrder;
    }

    void VFXRibbonGPUPipeline::removeEmitter(uint32_t emitterIndex)
    {
        auto configIt = emitterConfigs.find(emitterIndex);
        if (configIt != emitterConfigs.end())
        {
            if (!configIt->second.texturePath.empty() && bindless)
            {
                bindless->release(configIt->second.texturePath, /*srgb=*/true);
            }
            emitterConfigs.erase(configIt);
        }
    }

    void VFXRibbonGPUPipeline::recordCommandsInline(
        vk::CommandBuffer cmd,
        vk::Buffer drawCommandBuffer,
        uint32_t emitterCount) const
    {
        if (!initialized || emitterCount == 0 || !cachedParticleBuffer ||
            !cachedRibbonRingBuffer || !cachedRibbonHeadBuffer || emitterConfigs.empty())
        {
            return;
        }

        writeDescriptors();

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        vk::Pipeline lastBoundPipeline = graphicsPipeline; // VK-1472: swapped to Multiply variant per-emitter

        // Bind lighting descriptor sets (sets 1-3) if available
        if (lightingAvailable && cachedLightBufferSet && cachedClusterGridSet && cachedClusterLightGridSet)
        {
            std::array<vk::DescriptorSet, 3> lightingSets = {
                cachedLightBufferSet, cachedClusterGridSet, cachedClusterLightGridSet
            };
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                   1, lightingSets, {});
        }

        // VK-1481: set 0 (camera/particle/config/depth/ring/head/lut) is constant across the pass — bind once.
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                               0, defaultDescriptorSet, {});

        // VK-1481: shared bindless texture set (set 4) — bind once; emitters pick a slot via push constant.
        if (bindless)
        {
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                   4, bindless->getDescriptorSet(), {});
        }

        vk::Buffer vertexBuffers[] = {quadVertexBuffer};
        vk::DeviceSize offsets[] = {0};
        cmd.bindVertexBuffers(0, 1, vertexBuffers, offsets);
        cmd.bindIndexBuffer(quadIndexBuffer, 0, vk::IndexType::eUint16);

        // VK-1471: submit ribbon emitter draws in ascending sortOrder. Built in the map's
        // current traversal order, so an all-default (0) set is a stable no-op.
        std::vector<::vfx::VFXDrawOrderEntry> drawOrder;
        drawOrder.reserve(emitterConfigs.size());
        for (const auto& [emitterIdx, config] : emitterConfigs)
        {
            drawOrder.push_back({emitterIdx, config.sortOrder});
        }
        ::vfx::stableSortDrawOrder(drawOrder);

        // VK-1481 Phase 2: collect ribbon-drawable slots (in sorted order), write their per-emitter
        // render-data, then merge into runs of CONSECUTIVE slots that share the graphics pipeline.
        auto* rd = static_cast<VFXEmitterRenderData*>(renderDataMapped);
        struct DrawSlot { uint32_t slot; bool multiply; };
        std::vector<DrawSlot> drawable;
        drawable.reserve(drawOrder.size());

        for (const auto& drawEntry : drawOrder)
        {
            const uint32_t emitterIdx = drawEntry.index;
            auto configIt = emitterConfigs.find(emitterIdx);
            if (configIt == emitterConfigs.end())
                continue;
            if (emitterIdx >= emitterCount)
                continue;
            const auto& config = configIt->second;

            if (rd)
            {
                VFXEmitterRenderData& e = rd[emitterIdx];
                e.textureIndex = config.textureIndex; // VK-1481: bindless slot, selected in-shader
                e.alphaClipThreshold = config.alphaClipThreshold;
                e.blendMode = config.blendMode;
                e.glowColorR = config.glowColor.r;
                e.glowColorG = config.glowColor.g;
                e.glowColorB = config.glowColor.b;
                e._pad0 = 0.0f;
                e._pad1 = 0.0f;
            }

            // VK-1472: Multiply emitters bind the dedicated Multiply blend pipeline (shares the layout).
            drawable.push_back({emitterIdx, config.blendMode == 3u && multiplyPipeline});
        }

        // Emit one multiDrawIndirect per run. A run extends while the next drawable slot is the
        // previous slot + 1 and uses the same pipeline (Multiply vs. normal). gl_DrawID within the
        // multi-draw recovers emitterSlot = runBaseSlot + gl_DrawID in the shader.
        size_t idx = 0;
        while (idx < drawable.size())
        {
            const uint32_t baseSlot = drawable[idx].slot;
            const bool multiply = drawable[idx].multiply;
            size_t j = idx + 1;
            while (j < drawable.size() &&
                   drawable[j].slot == drawable[j - 1].slot + 1 &&
                   drawable[j].multiply == multiply)
            {
                ++j;
            }
            const uint32_t runLen = static_cast<uint32_t>(j - idx);

            vk::Pipeline wantPipeline = multiply ? multiplyPipeline : graphicsPipeline;
            if (wantPipeline != lastBoundPipeline)
            {
                cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, wantPipeline);
                lastBoundPipeline = wantPipeline;
            }

            GPUVFXMergedPushConstants pushConstants{};
            pushConstants.runBaseSlot = baseSlot;
            cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eVertex,
                              0, sizeof(GPUVFXMergedPushConstants), &pushConstants);

            const vk::DeviceSize offset = static_cast<vk::DeviceSize>(baseSlot) * sizeof(VFXDrawIndirectCommand);
            cmd.drawIndexedIndirect(drawCommandBuffer, offset, runLen, sizeof(VFXDrawIndirectCommand));
            render::FrameDrawStats::count(render::DrawCategory::VFX);

            idx = j;
        }
    }
}
