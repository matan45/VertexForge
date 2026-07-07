#include "VFXSceneGPUPipeline.hpp"
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
    void VFXSceneGPUPipeline::updateCameraUBO(
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

    void VFXSceneGPUPipeline::setSceneDepthImageView(vk::ImageView depthView)
    {
        if (sceneDepthImageView != depthView)
        {
            sceneDepthImageView = depthView;
            descriptorsNeedUpdate = true;
        }
    }

    void VFXSceneGPUPipeline::updateParticleBuffer(vk::Buffer particleBuffer, vk::DeviceSize particleBufferSize)
    {
        if (particleBuffer != cachedParticleBuffer || particleBufferSize != cachedParticleBufferSize)
        {
            cachedParticleBuffer = particleBuffer;
            cachedParticleBufferSize = particleBufferSize;
            descriptorsNeedUpdate = true;
        }
    }

    void VFXSceneGPUPipeline::updateConfigBuffer(vk::Buffer configBuffer, vk::DeviceSize configBufferSize)
    {
        if (configBuffer != cachedConfigBuffer || configBufferSize != cachedConfigBufferSize)
        {
            cachedConfigBuffer = configBuffer;
            cachedConfigBufferSize = configBufferSize;
            descriptorsNeedUpdate = true;
        }
    }

    void VFXSceneGPUPipeline::writeDescriptors() const
    {
        if (!descriptorsNeedUpdate || !cachedParticleBuffer || !cachedConfigBuffer)
        {
            return;
        }

        writeDescriptorSet(defaultDescriptorSet);

        descriptorsNeedUpdate = false;
    }

    void VFXSceneGPUPipeline::writeDescriptorSet(vk::DescriptorSet dstSet) const
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

        vk::DescriptorBufferInfo renderDataInfo{};
        renderDataInfo.buffer = renderDataBuffer;
        renderDataInfo.offset = 0;
        renderDataInfo.range = sizeof(VFXEmitterRenderData) * GPUVFXConstants::MAX_EMITTERS;

        // VK-1481: binding 1 (per-emitter texture) is gone; the texture lives in the bindless set.
        std::array<vk::WriteDescriptorSet, 5> writes{};

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

        // VK-1481 Phase 2: per-emitter render-data SSBO
        writes[4].dstSet = dstSet;
        writes[4].dstBinding = 8;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[4].pBufferInfo = &renderDataInfo;

        vkDevice.updateDescriptorSets(writes, {});
    }

    void VFXSceneGPUPipeline::setEmitterTexture(uint32_t emitterIndex, const std::string& texturePath)
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

    void VFXSceneGPUPipeline::setEmitterRenderingConfig(uint32_t emitterIndex,
                                                         float alphaClipThreshold, ::vfx::VFXBlendMode blendMode,
                                                         const glm::vec3& glowColor, int32_t sortOrder)
    {
        emitterConfigs[emitterIndex].alphaClipThreshold = alphaClipThreshold;
        emitterConfigs[emitterIndex].blendMode = ::vfx::blendModeToGpuValue(blendMode);
        emitterConfigs[emitterIndex].glowColor = glowColor;
        emitterConfigs[emitterIndex].sortOrder = sortOrder;
    }

    void VFXSceneGPUPipeline::setEmitterRenderMode(uint32_t emitterIndex, uint32_t renderMode)
    {
        emitterConfigs[emitterIndex].renderMode = renderMode;
    }

    void VFXSceneGPUPipeline::setEmitterDistortionEnabled(uint32_t emitterIndex, bool enabled)
    {
        emitterConfigs[emitterIndex].distortionEnabled = enabled;
    }

    void VFXSceneGPUPipeline::removeEmitter(uint32_t emitterIndex)
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

    void VFXSceneGPUPipeline::recordCommandsInline(
        vk::CommandBuffer cmd,
        vk::Buffer drawCommandBuffer,
        uint32_t emitterCount) const
    {
        if (!initialized || emitterCount == 0 || !cachedParticleBuffer)
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

        // VK-1481: set 0 (camera/particle/config/depth) is constant across the pass — bind once.
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

        // VK-1471: submit emitter draws in ascending sortOrder (lower = drawn behind); stable so a
        // uniform sortOrder reproduces slot order exactly.
        std::vector<::vfx::VFXDrawOrderEntry> drawOrder;
        drawOrder.reserve(emitterCount);
        for (uint32_t slot = 0; slot < emitterCount; ++slot)
        {
            auto cfgIt = emitterConfigs.find(slot);
            const int32_t so = (cfgIt != emitterConfigs.end()) ? cfgIt->second.sortOrder : 0;
            drawOrder.push_back({slot, so});
        }
        ::vfx::stableSortDrawOrder(drawOrder);

        // VK-1481 Phase 2: collect billboard-drawable slots (in sorted order), write their per-emitter
        // render-data, then merge into runs of CONSECUTIVE slots that share the graphics pipeline.
        auto* rd = static_cast<VFXEmitterRenderData*>(renderDataMapped);
        struct DrawSlot { uint32_t slot; bool multiply; };
        std::vector<DrawSlot> drawable;
        drawable.reserve(drawOrder.size());

        for (const auto& drawEntry : drawOrder)
        {
            const uint32_t i = drawEntry.index;
            auto configIt = emitterConfigs.find(i);
            // Skip mesh/ribbon (handled by dedicated pipelines) and distortion (separate vector pass).
            if (configIt != emitterConfigs.end() &&
                (configIt->second.renderMode == RenderModeFlags::MeshParticle ||
                 configIt->second.renderMode == RenderModeFlags::Ribbon ||
                 configIt->second.distortionEnabled))
            {
                continue;
            }

            uint32_t blendMode = 0;
            if (rd)
            {
                VFXEmitterRenderData& e = rd[i];
                if (configIt != emitterConfigs.end())
                {
                    e.textureIndex = configIt->second.textureIndex;
                    e.alphaClipThreshold = configIt->second.alphaClipThreshold;
                    e.blendMode = configIt->second.blendMode;
                    e.glowColorR = configIt->second.glowColor.r;
                    e.glowColorG = configIt->second.glowColor.g;
                    e.glowColorB = configIt->second.glowColor.b;
                    blendMode = configIt->second.blendMode;
                }
                else
                {
                    // Inactive slot (no config): a no-op sub-draw (instanceCount 0) — default data.
                    e.textureIndex = bindless ? bindless->defaultWhiteIndex() : 0u;
                    e.alphaClipThreshold = 0.1f;
                    e.blendMode = 0;
                    e.glowColorR = e.glowColorG = e.glowColorB = 1.0f;
                }
            }

            drawable.push_back({i, blendMode == 3u && multiplyPipeline});
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

            // VK-1472: Multiply run binds the dedicated Multiply blend pipeline (shared layout).
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
