#include "VFXSceneGPUPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/Texture.hpp"
#include "print/Logger.hpp"
#include "GPUVFXTypes.hpp"
#include <filesystem>

namespace render::vfx
{
    void VFXSceneGPUPipeline::updateCameraUBO(
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPos,
        float time) const
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

        std::memcpy(cameraUBOMapped, &ubo, sizeof(ubo));
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

        writeDescriptorSet(defaultDescriptorSet, nullptr);

        for (const auto& [path, entry] : textureEntries)
        {
            writeDescriptorSet(entry.descriptorSet, entry.texture.get());
        }

        descriptorsNeedUpdate = false;
    }

    void VFXSceneGPUPipeline::writeDescriptorSet(vk::DescriptorSet dstSet, core::Texture* texture) const
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

        std::array<vk::WriteDescriptorSet, 4> writes{};

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

        vkDevice.updateDescriptorSets(writes, {});
    }

    void VFXSceneGPUPipeline::setEmitterTexture(uint32_t emitterIndex, const std::string& texturePath)
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
            loggerWarning("VFX GPU texture not found: {}", texturePath);
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

            if (cachedParticleBuffer && cachedConfigBuffer)
            {
                writeDescriptorSet(entry.descriptorSet, entry.texture.get());
            }
            else
            {
                descriptorsNeedUpdate = true;
            }

            loggerInfo("VFX GPU texture loaded: {}", texturePath);
        }
        catch (const std::exception& e)
        {
            loggerError("Failed to load VFX GPU texture '{}': {}", texturePath, e.what());
            textureEntries.erase(texturePath);
            emitterConfigs[emitterIndex].texturePath.clear();
        }
    }

    void VFXSceneGPUPipeline::setEmitterRenderingConfig(uint32_t emitterIndex,
                                                         float alphaClipThreshold, bool additiveBlend)
    {
        emitterConfigs[emitterIndex].alphaClipThreshold = alphaClipThreshold;
        emitterConfigs[emitterIndex].blendMode = additiveBlend ? 1u : 0u;
    }

    void VFXSceneGPUPipeline::removeEmitter(uint32_t emitterIndex)
    {
        emitterConfigs.erase(emitterIndex);
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

        vk::Buffer vertexBuffers[] = {quadVertexBuffer};
        vk::DeviceSize offsets[] = {0};
        cmd.bindVertexBuffers(0, 1, vertexBuffers, offsets);
        cmd.bindIndexBuffer(quadIndexBuffer, 0, vk::IndexType::eUint16);

        vk::DescriptorSet lastBoundSet = nullptr;

        for (uint32_t i = 0; i < emitterCount; ++i)
        {
            vk::DescriptorSet setToBind = defaultDescriptorSet;
            float alphaClip = 0.1f;
            uint32_t blendMode = 0;

            auto configIt = emitterConfigs.find(i);
            if (configIt != emitterConfigs.end())
            {
                alphaClip = configIt->second.alphaClipThreshold;
                blendMode = configIt->second.blendMode;

                if (!configIt->second.texturePath.empty())
                {
                    auto texIt = textureEntries.find(configIt->second.texturePath);
                    if (texIt != textureEntries.end())
                    {
                        setToBind = texIt->second.descriptorSet;
                    }
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
            cmd.pushConstants(pipelineLayout,
                              vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                              0, sizeof(GPUVFXBillboardPushConstants), &pushConstants);

            vk::DeviceSize offset = i * sizeof(VFXDrawIndirectCommand);
            cmd.drawIndexedIndirect(drawCommandBuffer, offset, 1, sizeof(VFXDrawIndirectCommand));
        }
    }
}
