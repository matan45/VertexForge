#include "VFXRibbonPreviewPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Texture.hpp"
#include "print/Logger.hpp"
#include <filesystem>

namespace render::vfx
{
    void VFXRibbonPreviewPipeline::updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                                     const glm::vec3& cameraPos, float time) const
    {
        if (!cameraUBOMapped)
        {
            return;
        }

        VFXCameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;
        ubo.time = time;

        std::memcpy(cameraUBOMapped, &ubo, sizeof(ubo));
    }

    void VFXRibbonPreviewPipeline::setRibbonSegments(const std::vector<VFXRibbonSegmentData>& segments)
    {
        if (segments.empty())
        {
            currentInstanceCount = 0;
            return;
        }

        currentInstanceCount = static_cast<uint32_t>(std::min(segments.size(),
                                                               static_cast<size_t>(maxInstances)));

        vk::DeviceSize bufferSize = sizeof(VFXRibbonSegmentData) * currentInstanceCount;
        std::memcpy(instanceBufferMapped, segments.data(), bufferSize);
    }

    void VFXRibbonPreviewPipeline::setTexture(const std::string& texturePath)
    {
        if (texturePath == currentTexturePath)
        {
            return;
        }

        device.getLogicalDevice().waitIdle();

        customTexture.reset();
        currentTexturePath.clear();

        if (texturePath.empty() || !std::filesystem::exists(texturePath))
        {
            if (!texturePath.empty())
            {
                loggerWarning("VFX ribbon preview texture not found: {}", texturePath);
            }
            updateDescriptorSet();
            return;
        }

        try
        {
            customTexture = std::make_unique<core::Texture>(device);
            customTexture->loadTextureFromFile(texturePath, vk::Format::eR8G8B8A8Srgb, false);
            currentTexturePath = texturePath;
        }
        catch (const std::exception& e)
        {
            loggerError("Failed to load VFX ribbon preview texture '{}': {}", texturePath, e.what());
            customTexture.reset();
            currentTexturePath.clear();
        }

        updateDescriptorSet();
    }

    void VFXRibbonPreviewPipeline::setRenderingConfig(float alphaClipThreshold, bool additiveBlend,
                                                        float ribbonWidth,
                                                        const glm::vec3& glowColor,
                                                        float uvScrollSpeedU, float uvScrollSpeedV)
    {
        pushConstants.alphaClipThreshold = alphaClipThreshold;
        pushConstants.blendMode = additiveBlend ? 1u : 0u;
        pushConstants.ribbonWidth = ribbonWidth;
        pushConstants.glowColorR = glowColor.r;
        pushConstants.glowColorG = glowColor.g;
        pushConstants.glowColorB = glowColor.b;
        pushConstants.uvScrollSpeedU = uvScrollSpeedU;
        pushConstants.uvScrollSpeedV = uvScrollSpeedV;
    }

    void VFXRibbonPreviewPipeline::updateDescriptorSet()
    {
        vk::DescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = cameraUBO;
        uboBufferInfo.offset = 0;
        uboBufferInfo.range = sizeof(VFXCameraUBO);

        vk::WriteDescriptorSet uboWrite{};
        uboWrite.dstSet = descriptorSet;
        uboWrite.dstBinding = 0;
        uboWrite.dstArrayElement = 0;
        uboWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        uboWrite.descriptorCount = 1;
        uboWrite.pBufferInfo = &uboBufferInfo;

        vk::DescriptorImageInfo textureImageInfo{};
        textureImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        if (customTexture)
        {
            textureImageInfo.imageView = customTexture->getImageView();
            textureImageInfo.sampler = customTexture->getSampler();
        }
        else
        {
            textureImageInfo.imageView = defaultTextureImageView;
            textureImageInfo.sampler = textureSampler;
        }

        vk::WriteDescriptorSet textureWrite{};
        textureWrite.dstSet = descriptorSet;
        textureWrite.dstBinding = 1;
        textureWrite.dstArrayElement = 0;
        textureWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        textureWrite.descriptorCount = 1;
        textureWrite.pImageInfo = &textureImageInfo;

        std::array<vk::WriteDescriptorSet, 2> descriptorWrites = {uboWrite, textureWrite};
        device.getLogicalDevice().updateDescriptorSets(descriptorWrites, nullptr);
    }

    void VFXRibbonPreviewPipeline::recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                                         uint32_t imageIndex) const
    {
        if (!initialized)
        {
            return;
        }

        std::array<vk::ClearValue, 2> clearValues{};
        clearValues[0].color = vk::ClearColorValue{std::array<float, 4>{0.1f, 0.1f, 0.1f, 1.0f}};
        clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

        vk::RenderPassBeginInfo renderPassInfo{};
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[imageIndex];
        renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderPassInfo.renderArea.extent = swapChain.getSwapchainExtent();
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

        if (currentInstanceCount > 0)
        {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                              0, descriptorSet, nullptr);

            vk::Buffer vertexBuffers[] = {quadVertexBuffer, instanceBuffer};
            vk::DeviceSize offsets[] = {0, 0};
            commandBuffer.bindVertexBuffers(0, 2, vertexBuffers, offsets);

            commandBuffer.bindIndexBuffer(quadIndexBuffer, 0, vk::IndexType::eUint16);

            commandBuffer.pushConstants(pipelineLayout,
                                        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                        0, sizeof(VFXRibbonPreviewPushConstants), &pushConstants);

            commandBuffer.drawIndexed(VFXConstants::QUAD_INDEX_COUNT, currentInstanceCount, 0, 0, 0);
        }

        commandBuffer.endRenderPass();
    }
}
