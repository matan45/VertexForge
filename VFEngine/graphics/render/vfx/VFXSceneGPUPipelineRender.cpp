#include "VFXSceneGPUPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/Texture.hpp"
#include "../../core/DeferredDeletionQueue.hpp"
#include "print/Log.hpp"
#include "GPUVFXTypes.hpp"
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

        vk::DescriptorImageInfo depthInfo{};
        depthInfo.imageView = sceneDepthImageView ? sceneDepthImageView : defaultTextureImageView;
        depthInfo.imageLayout = sceneDepthImageView
            ? vk::ImageLayout::eDepthStencilReadOnlyOptimal
            : vk::ImageLayout::eShaderReadOnlyOptimal;
        depthInfo.sampler = depthSampler ? depthSampler : textureSampler;

        std::array<vk::WriteDescriptorSet, 5> writes{};

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

        vkDevice.updateDescriptorSets(writes, {});
    }

    void VFXSceneGPUPipeline::setEmitterTexture(uint32_t emitterIndex, const std::string& texturePath)
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
            vfLogWarning("VFX GPU texture not found: {}", texturePath);
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

            if (cachedParticleBuffer && cachedConfigBuffer)
            {
                writeDescriptorSet(entry.descriptorSet, entry.texture.get());
            }
            else
            {
                descriptorsNeedUpdate = true;
            }

            vfLogInfo("VFX GPU texture loaded: {}", texturePath);
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to load VFX GPU texture '{}': {}", texturePath, e.what());
            textureEntries.erase(texturePath);
            emitterConfigs[emitterIndex].texturePath.clear();
        }
    }

    void VFXSceneGPUPipeline::setEmitterRenderingConfig(uint32_t emitterIndex,
                                                         float alphaClipThreshold, bool additiveBlend,
                                                         const glm::vec3& glowColor)
    {
        emitterConfigs[emitterIndex].alphaClipThreshold = alphaClipThreshold;
        emitterConfigs[emitterIndex].blendMode = additiveBlend ? 1u : 0u;
        emitterConfigs[emitterIndex].glowColor = glowColor;
    }

    void VFXSceneGPUPipeline::setEmitterRenderMode(uint32_t emitterIndex, uint32_t renderMode)
    {
        emitterConfigs[emitterIndex].renderMode = renderMode;
    }

    void VFXSceneGPUPipeline::removeEmitter(uint32_t emitterIndex)
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

    void VFXSceneGPUPipeline::recordCommandsInline(
        vk::CommandBuffer cmd,
        vk::Buffer drawCommandBuffer,
        uint32_t emitterCount) const
    {
        ++frameCounter;

        if (!initialized || emitterCount == 0 || !cachedParticleBuffer)
        {
            return;
        }

        writeDescriptors();

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

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

        for (uint32_t i = 0; i < emitterCount; ++i)
        {
            auto configIt = emitterConfigs.find(i);
            if (configIt != emitterConfigs.end() &&
                (configIt->second.renderMode == RenderModeFlags::MeshParticle ||
                 configIt->second.renderMode == RenderModeFlags::Ribbon))
            {
                continue;
            }

            vk::DescriptorSet setToBind = defaultDescriptorSet;
            float alphaClip = 0.1f;
            uint32_t blendMode = 0;
            glm::vec3 gc(1.0f);

            if (configIt != emitterConfigs.end())
            {
                alphaClip = configIt->second.alphaClipThreshold;
                blendMode = configIt->second.blendMode;
                gc = configIt->second.glowColor;

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
            pushConstants.glowColorR = gc.r;
            pushConstants.glowColorG = gc.g;
            pushConstants.glowColorB = gc.b;
            cmd.pushConstants(pipelineLayout,
                              vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                              0, sizeof(GPUVFXBillboardPushConstants), &pushConstants);

            vk::DeviceSize offset = i * sizeof(VFXDrawIndirectCommand);
            cmd.drawIndexedIndirect(drawCommandBuffer, offset, 1, sizeof(VFXDrawIndirectCommand));
        }
    }
}
