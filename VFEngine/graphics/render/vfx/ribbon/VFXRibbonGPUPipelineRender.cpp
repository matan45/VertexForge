#include "VFXRibbonGPUPipeline.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Texture.hpp"
#include "../../../core/DeferredDeletionQueue.hpp"
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

        writeDescriptorSet(defaultDescriptorSet, nullptr);

        for (const auto& [path, entry] : textureEntries)
        {
            writeDescriptorSet(entry.descriptorSet, entry.texture.get());
        }

        descriptorsNeedUpdate = false;
    }

    void VFXRibbonGPUPipeline::writeDescriptorSet(vk::DescriptorSet dstSet, core::Texture* texture) const
    {
        auto vkDevice = device.getLogicalDevice();

        vk::DescriptorBufferInfo cameraInfo{};
        cameraInfo.buffer = cameraUBO;
        cameraInfo.offset = 0;
        cameraInfo.range = sizeof(GPUVFXCameraUBO);

        vk::DescriptorImageInfo textureInfo{};
        textureInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        if (texture)
        {
            textureInfo.imageView = texture->getImageView();
            textureInfo.sampler = texture->getSampler();
        }
        else
        {
            textureInfo.imageView = defaultTextureImageView;
            textureInfo.sampler = textureSampler;
        }

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

        std::array<vk::WriteDescriptorSet, 8> writes{};

        writes[0].dstSet = dstSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[0].pBufferInfo = &cameraInfo;

        writes[1].dstSet = dstSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[1].pImageInfo = &textureInfo;

        writes[2].dstSet = dstSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[2].pBufferInfo = &particleInfo;

        writes[3].dstSet = dstSet;
        writes[3].dstBinding = 3;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[3].pBufferInfo = &configInfo;

        writes[4].dstSet = dstSet;
        writes[4].dstBinding = 4;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[4].pImageInfo = &depthInfo;

        writes[5].dstSet = dstSet;
        writes[5].dstBinding = 5;
        writes[5].descriptorCount = 1;
        writes[5].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[5].pBufferInfo = &ringInfo;

        writes[6].dstSet = dstSet;
        writes[6].dstBinding = 6;
        writes[6].descriptorCount = 1;
        writes[6].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[6].pBufferInfo = &headInfo;

        writes[7].dstSet = dstSet;
        writes[7].dstBinding = 7;
        writes[7].descriptorCount = 1;
        writes[7].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[7].pBufferInfo = &lutInfo;

        vkDevice.updateDescriptorSets(writes, {});
    }

    void VFXRibbonGPUPipeline::setEmitterTexture(uint32_t emitterIndex, const std::string& texturePath)
    {
        auto& config = emitterConfigs[emitterIndex];
        const std::string oldPath = config.texturePath;
        config.texturePath = texturePath;

        if (oldPath == texturePath)
            return;

        if (!oldPath.empty())
        {
            auto it = textureEntries.find(oldPath);
            if (it != textureEntries.end() && --it->second.refCount == 0)
            {
                if (deletionQueue && it->second.texture)
                {
                    it->second.texture->extractResources(*deletionQueue);
                }
                if (it->second.descriptorSet)
                {
                    pendingDescriptorSets.push_back({it->second.descriptorSet, frameCounter});
                }
                textureEntries.erase(it);
            }
        }

        if (texturePath.empty())
        {
            return;
        }

        if (textureEntries.count(texturePath))
        {
            textureEntries[texturePath].refCount++;
            return;
        }

        if (!std::filesystem::exists(texturePath))
        {
            vfLogWarning("VFX ribbon texture not found: {}", texturePath);
            emitterConfigs[emitterIndex].texturePath.clear();
            return;
        }

        if (textureEntries.size() >= MAX_TEXTURE_SLOTS)
        {
            vfLogWarning("Max VFX texture slots ({}) reached, emitter {} will use default texture",
                          MAX_TEXTURE_SLOTS, emitterIndex);
            emitterConfigs[emitterIndex].texturePath.clear();
            return;
        }

        if (!deletionQueue)
        {
            device.getLogicalDevice().waitIdle();
        }

        try
        {
            auto& entry = textureEntries[texturePath];
            entry.texture = std::make_unique<core::Texture>(device);
            entry.texture->loadTextureFromFile(texturePath, vk::Format::eR8G8B8A8Srgb, false);
            entry.descriptorSet = allocateDescriptorSetFromPool();
            entry.refCount = 1;

            if (cachedParticleBuffer && cachedConfigBuffer &&
                cachedRibbonRingBuffer && cachedRibbonHeadBuffer)
            {
                writeDescriptorSet(entry.descriptorSet, entry.texture.get());
            }
            else
            {
                descriptorsNeedUpdate = true;
            }

            vfLogInfo("VFX ribbon texture loaded: {}", texturePath);
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to load VFX ribbon texture '{}': {}", texturePath, e.what());
            textureEntries.erase(texturePath);
            emitterConfigs[emitterIndex].texturePath.clear();
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
            const auto& texPath = configIt->second.texturePath;
            if (!texPath.empty())
            {
                auto texIt = textureEntries.find(texPath);
                if (texIt != textureEntries.end() && --texIt->second.refCount == 0)
                {
                    if (deletionQueue && texIt->second.texture)
                    {
                        texIt->second.texture->extractResources(*deletionQueue);
                    }
                    if (texIt->second.descriptorSet)
                    {
                        pendingDescriptorSets.push_back({texIt->second.descriptorSet, frameCounter});
                    }
                    textureEntries.erase(texIt);
                }
            }
            emitterConfigs.erase(configIt);
        }
    }

    void VFXRibbonGPUPipeline::recordCommandsInline(
        vk::CommandBuffer cmd,
        vk::Buffer drawCommandBuffer,
        uint32_t emitterCount) const
    {
        ++frameCounter;

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

        vk::Buffer vertexBuffers[] = {quadVertexBuffer};
        vk::DeviceSize offsets[] = {0};
        cmd.bindVertexBuffers(0, 1, vertexBuffers, offsets);
        cmd.bindIndexBuffer(quadIndexBuffer, 0, vk::IndexType::eUint16);

        vk::DescriptorSet lastBoundSet = nullptr;

        // VK-1471: submit ribbon emitter draws in ascending sortOrder. Built in the map's
        // current traversal order, so an all-default (0) set is a stable no-op.
        std::vector<::vfx::VFXDrawOrderEntry> drawOrder;
        drawOrder.reserve(emitterConfigs.size());
        for (const auto& [emitterIdx, config] : emitterConfigs)
        {
            drawOrder.push_back({emitterIdx, config.sortOrder});
        }
        ::vfx::stableSortDrawOrder(drawOrder);

        for (const auto& drawEntry : drawOrder)
        {
            const uint32_t emitterIdx = drawEntry.index;
            auto configIt = emitterConfigs.find(emitterIdx);
            if (configIt == emitterConfigs.end())
                continue;
            const auto& config = configIt->second;

            if (emitterIdx >= emitterCount)
                continue;

            vk::DescriptorSet setToBind = defaultDescriptorSet;
            float alphaClip = config.alphaClipThreshold;
            uint32_t blendMode = config.blendMode;
            glm::vec3 gc = config.glowColor;

            if (!config.texturePath.empty())
            {
                auto texIt = textureEntries.find(config.texturePath);
                if (texIt != textureEntries.end())
                {
                    setToBind = texIt->second.descriptorSet;
                }
            }

            if (setToBind != lastBoundSet)
            {
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                       0, setToBind, {});
                lastBoundSet = setToBind;
            }

            // VK-1472: Multiply emitters bind the dedicated Multiply blend pipeline (shares the
            // layout, so the descriptor sets + vertex/index bindings above stay valid).
            vk::Pipeline wantPipeline = (blendMode == 3u && multiplyPipeline) ? multiplyPipeline : graphicsPipeline;
            if (wantPipeline != lastBoundPipeline)
            {
                cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, wantPipeline);
                lastBoundPipeline = wantPipeline;
            }

            GPUVFXBillboardPushConstants pushConstants{};
            pushConstants.emitterIndex = emitterIdx;
            pushConstants.alphaClipThreshold = alphaClip;
            pushConstants.blendMode = blendMode;
            pushConstants.glowColorR = gc.r;
            pushConstants.glowColorG = gc.g;
            pushConstants.glowColorB = gc.b;
            cmd.pushConstants(pipelineLayout,
                              vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                              0, sizeof(GPUVFXBillboardPushConstants), &pushConstants);

            vk::DeviceSize offset = emitterIdx * sizeof(VFXDrawIndirectCommand);
            cmd.drawIndexedIndirect(drawCommandBuffer, offset, 1, sizeof(VFXDrawIndirectCommand));
            render::FrameDrawStats::count(render::DrawCategory::VFX);
        }
    }
}
