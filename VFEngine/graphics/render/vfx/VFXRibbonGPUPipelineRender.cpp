#include "VFXRibbonGPUPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/Texture.hpp"
#include "print/Logger.hpp"
#include "GPUVFXTypes.hpp"
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

    void VFXRibbonGPUPipeline::writeDescriptors() const
    {
        if (!descriptorsNeedUpdate || !cachedParticleBuffer || !cachedConfigBuffer ||
            !cachedRibbonRingBuffer || !cachedRibbonHeadBuffer)
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
        depthInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        depthInfo.imageView = sceneDepthImageView ? sceneDepthImageView : defaultTextureImageView;
        depthInfo.sampler = depthSampler ? depthSampler : textureSampler;

        vk::DescriptorBufferInfo ringInfo{};
        ringInfo.buffer = cachedRibbonRingBuffer;
        ringInfo.offset = 0;
        ringInfo.range = cachedRibbonRingBufferSize;

        vk::DescriptorBufferInfo headInfo{};
        headInfo.buffer = cachedRibbonHeadBuffer;
        headInfo.offset = 0;
        headInfo.range = cachedRibbonHeadBufferSize;

        std::array<vk::WriteDescriptorSet, 7> writes{};

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

        vkDevice.updateDescriptorSets(writes, {});
    }

    void VFXRibbonGPUPipeline::setEmitterTexture(uint32_t emitterIndex, const std::string& texturePath)
    {
        emitterConfigs[emitterIndex].texturePath = texturePath;

        if (texturePath.empty())
        {
            return;
        }

        if (textureEntries.count(texturePath))
        {
            return;
        }

        if (!std::filesystem::exists(texturePath))
        {
            loggerWarning("VFX ribbon texture not found: {}", texturePath);
            emitterConfigs[emitterIndex].texturePath.clear();
            return;
        }

        if (textureEntries.size() >= MAX_TEXTURE_SLOTS)
        {
            loggerWarning("Max VFX texture slots ({}) reached, emitter {} will use default texture",
                          MAX_TEXTURE_SLOTS, emitterIndex);
            emitterConfigs[emitterIndex].texturePath.clear();
            return;
        }

        device.getLogicalDevice().waitIdle();

        try
        {
            auto& entry = textureEntries[texturePath];
            entry.texture = std::make_unique<core::Texture>(device);
            entry.texture->loadTextureFromFile(texturePath, vk::Format::eR8G8B8A8Srgb, false);
            entry.descriptorSet = allocateDescriptorSetFromPool();

            if (cachedParticleBuffer && cachedConfigBuffer &&
                cachedRibbonRingBuffer && cachedRibbonHeadBuffer)
            {
                writeDescriptorSet(entry.descriptorSet, entry.texture.get());
            }
            else
            {
                descriptorsNeedUpdate = true;
            }

            loggerInfo("VFX ribbon texture loaded: {}", texturePath);
        }
        catch (const std::exception& e)
        {
            loggerError("Failed to load VFX ribbon texture '{}': {}", texturePath, e.what());
            textureEntries.erase(texturePath);
            emitterConfigs[emitterIndex].texturePath.clear();
        }
    }

    void VFXRibbonGPUPipeline::setEmitterRenderingConfig(uint32_t emitterIndex,
                                                           float alphaClipThreshold, bool additiveBlend,
                                                           const glm::vec3& glowColor)
    {
        emitterConfigs[emitterIndex].alphaClipThreshold = alphaClipThreshold;
        emitterConfigs[emitterIndex].blendMode = additiveBlend ? 1u : 0u;
        emitterConfigs[emitterIndex].glowColor = glowColor;
    }

    void VFXRibbonGPUPipeline::removeEmitter(uint32_t emitterIndex)
    {
        emitterConfigs.erase(emitterIndex);
    }

    void VFXRibbonGPUPipeline::recordCommandsInline(
        vk::CommandBuffer cmd,
        vk::Buffer drawCommandBuffer,
        uint32_t emitterCount) const
    {
        if (!initialized || emitterCount == 0 || !cachedParticleBuffer ||
            !cachedRibbonRingBuffer || !cachedRibbonHeadBuffer)
        {
            return;
        }

        writeDescriptors();

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        vk::Buffer vertexBuffers[] = {quadVertexBuffer};
        vk::DeviceSize offsets[] = {0};
        cmd.bindVertexBuffers(0, 1, vertexBuffers, offsets);
        cmd.bindIndexBuffer(quadIndexBuffer, 0, vk::IndexType::eUint16);

        vk::DescriptorSet lastBoundSet = nullptr;

        for (uint32_t i = 0; i < emitterCount; ++i)
        {
            // Only render emitters that have ribbon config
            auto configIt = emitterConfigs.find(i);
            if (configIt == emitterConfigs.end())
            {
                continue;
            }

            vk::DescriptorSet setToBind = defaultDescriptorSet;
            float alphaClip = configIt->second.alphaClipThreshold;
            uint32_t blendMode = configIt->second.blendMode;
            glm::vec3 gc = configIt->second.glowColor;

            if (!configIt->second.texturePath.empty())
            {
                auto texIt = textureEntries.find(configIt->second.texturePath);
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

            GPUVFXBillboardPushConstants pushConstants{};
            pushConstants.emitterIndex = i;
            pushConstants.alphaClipThreshold = alphaClip;
            pushConstants.blendMode = blendMode;
            pushConstants.glowColorR = gc.r;
            pushConstants.glowColorG = gc.g;
            pushConstants.glowColorB = gc.b;
            cmd.pushConstants(pipelineLayout,
                              vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                              0, sizeof(GPUVFXBillboardPushConstants), &pushConstants);

            // Use drawIndexedIndirect from shared draw command buffer
            // Compute shader sets instanceCount = min(head, maxTP) - 1 for ribbon emitters
            vk::DeviceSize offset = i * sizeof(VFXDrawIndirectCommand);
            cmd.drawIndexedIndirect(drawCommandBuffer, offset, 1, sizeof(VFXDrawIndirectCommand));
        }
    }
}
