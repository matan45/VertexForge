#include "UIRenderPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Texture.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "../../core/ImageUtilities.hpp"
#include "resource/Types.hpp"
#include "print/Log.hpp"
#include <filesystem>
#include <algorithm>

namespace render::ui
{
    bool UIRenderPipeline::loadTexture(const std::string& texturePath)
    {
        if (textureCache.contains(texturePath)) return true;

        if (textureCache.size() >= MAX_UI_TEXTURES)
        {
            vfLogWarning("UI texture limit reached ({}), cannot load: {}", MAX_UI_TEXTURES, texturePath);
            return false;
        }

        if (!std::filesystem::exists(texturePath))
        {
            vfLogWarning("UI texture file not found: {}", texturePath);
            return false;
        }

        auto texture = std::make_unique<core::Texture>(device);
        texture->loadTextureFromFile(texturePath, vk::Format::eR8G8B8A8Unorm, false);

        if (!texture->getImageView())
        {
            vfLogError("Failed to load UI texture: {}", texturePath);
            return false;
        }

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        vk::DescriptorSet newDescSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
        updateDescriptorSet(newDescSet, texture->getImageView(), texture->getSampler());

        TextureEntry entry;
        entry.texture = std::move(texture);
        entry.descriptorSet = newDescSet;
        textureCache.emplace(texturePath, std::move(entry));

        return true;
    }

    void UIRenderPipeline::registerExternalTexture(const std::string& key,
                                                     vk::ImageView imageView, vk::Sampler externalSampler)
    {
        if (!initialized || !imageView || !externalSampler) return;

        auto it = externalTextureCache.find(key);
        if (it != externalTextureCache.end())
        {
            updateDescriptorSet(it->second, imageView, externalSampler);
            return;
        }

        if (externalTextureCache.size() >= MAX_EXTERNAL_TEXTURES) return;

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        vk::DescriptorSet newDescSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
        updateDescriptorSet(newDescSet, imageView, externalSampler);
        externalTextureCache[key] = newDescSet;
    }

    void UIRenderPipeline::unregisterExternalTexture(const std::string& key)
    {
        auto it = externalTextureCache.find(key);
        if (it != externalTextureCache.end())
        {
            device.getLogicalDevice().freeDescriptorSets(descriptorPool, it->second);
            externalTextureCache.erase(it);
        }
    }

    void UIRenderPipeline::clearExternalTextures()
    {
        for (auto& [key, descSet] : externalTextureCache)
            device.getLogicalDevice().freeDescriptorSets(descriptorPool, descSet);
        externalTextureCache.clear();
    }

    void UIRenderPipeline::setUIImageDrawList(const std::vector<UIImageRenderData>& images)
    {
        scissorGroups.clear();
        totalInstanceCount = 0;

        if (images.empty())
        {
            bufferManager.updateInstanceBuffer({});
            return;
        }

        std::vector<UIImageInstance> allInstances;
        allInstances.reserve(images.size());

        UIScissorGroup* currentGroup = nullptr;
        glm::ivec4 currentScissor{-1};

        for (const auto& image : images)
        {
            if (image.texturePath.empty()) continue;

            std::string resolvedPath = image.texturePath;
            bool isRTTSynthetic = resolvedPath.starts_with("__rtt_");
            bool hasTexture = externalTextureCache.contains(resolvedPath)
                           || (!isRTTSynthetic && loadTexture(resolvedPath));
            if (!hasTexture) continue;

            glm::ivec4 scissorKey{
                static_cast<int32_t>(image.scissorRect.x), static_cast<int32_t>(image.scissorRect.y),
                static_cast<int32_t>(image.scissorRect.z), static_cast<int32_t>(image.scissorRect.w)
            };

            if (!currentGroup || scissorKey != currentScissor)
            {
                scissorGroups.emplace_back();
                currentGroup = &scissorGroups.back();
                currentGroup->scissorRect = glm::vec4(scissorKey);
                currentScissor = scissorKey;
            }

            bool canExtend = false;
            if (!currentGroup->batches.empty())
            {
                auto& lastBatch = currentGroup->batches.back();
                canExtend = (lastBatch.texturePath == resolvedPath
                          && lastBatch.stencilOp == image.stencilOp
                          && lastBatch.stencilRef == image.stencilRef
                          && lastBatch.discardColor == image.discardColor
                          && lastBatch.alphaThreshold == image.alphaThreshold);
            }

            UIImageInstance inst{};
            inst.posAndSize = glm::vec4(image.position, image.size);
            inst.colorTint = image.colorTint;
            inst.uvRect = image.uvRect;

            if (canExtend)
            {
                currentGroup->batches.back().instanceCount++;
            }
            else
            {
                UITextureBatch batch;
                batch.texturePath = resolvedPath;
                batch.firstInstance = static_cast<uint32_t>(allInstances.size());
                batch.instanceCount = 1;
                batch.stencilOp = image.stencilOp;
                batch.stencilRef = image.stencilRef;
                batch.discardColor = image.discardColor;
                batch.alphaThreshold = image.alphaThreshold;
                currentGroup->batches.push_back(std::move(batch));
            }

            allInstances.push_back(inst);
        }

        totalInstanceCount = static_cast<uint32_t>(allInstances.size());
        bufferManager.updateInstanceBuffer(allInstances);
    }

    void UIRenderPipeline::recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        if (!initialized || totalInstanceCount == 0) return;

        bool hasDisplay = !offscreenResources.displayColorImages.empty();
        auto& colorSrc = hasDisplay ? offscreenResources.displayColorImages : offscreenResources.colorImages;

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            colorSrc[imageIndex].colorImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        auto colorAttach = core::colorLoad(colorSrc[imageIndex].colorImageView);
        auto stencilAttach = core::stencilClear(offscreenResources.uiStencilImage.stencilImageView, 0);

        core::DynamicRenderingInfo info{};
        info.extent = swapChain.getDisplayExtent();
        info.colorAttachments = {colorAttach};
        info.stencilAttachment = stencilAttach;

        core::beginDynamicRendering(commandBuffer, info);

        vk::Pipeline currentPipeline = nullptr;

        vk::Buffer vertexBuffers[] = {bufferManager.getQuadVertexBuffer(), bufferManager.getInstanceBuffer()};
        vk::DeviceSize offsets[] = {0, 0};
        commandBuffer.bindVertexBuffers(0, 2, vertexBuffers, offsets);
        commandBuffer.bindIndexBuffer(bufferManager.getQuadIndexBuffer(), 0, vk::IndexType::eUint16);

        glm::vec2 viewportSize(static_cast<float>(swapChain.getDisplayExtent().width),
                               static_cast<float>(swapChain.getDisplayExtent().height));

        for (const auto& group : scissorGroups)
        {
            vk::Rect2D scissor{};
            if (group.scissorRect.z > 0.0f && group.scissorRect.w > 0.0f)
            {
                scissor.offset.x = static_cast<int32_t>(group.scissorRect.x);
                scissor.offset.y = static_cast<int32_t>(group.scissorRect.y);
                scissor.extent.width = static_cast<uint32_t>(group.scissorRect.z);
                scissor.extent.height = static_cast<uint32_t>(group.scissorRect.w);
            }
            else
            {
                scissor.offset = vk::Offset2D{0, 0};
                scissor.extent = swapChain.getDisplayExtent();
            }
            commandBuffer.setScissor(0, 1, &scissor);

            for (const auto& batch : group.batches)
            {
                vk::DescriptorSet texDescSet;
                auto it = textureCache.find(batch.texturePath);
                if (it != textureCache.end())
                    texDescSet = it->second.descriptorSet;
                else
                {
                    auto extIt = externalTextureCache.find(batch.texturePath);
                    if (extIt != externalTextureCache.end()) texDescSet = extIt->second;
                    else continue;
                }

                vk::Pipeline targetPipeline;
                switch (batch.stencilOp)
                {
                case UIStencilOp::Write:  targetPipeline = batch.discardColor ? pipelineStencilIncNoColor : pipelineStencilIncColor; break;
                case UIStencilOp::Test:   targetPipeline = pipelineStencilTest; break;
                case UIStencilOp::Restore: targetPipeline = pipelineStencilDecNoColor; break;
                default: targetPipeline = pipelineNormal; break;
                }

                if (targetPipeline != currentPipeline)
                {
                    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, targetPipeline);
                    currentPipeline = targetPipeline;
                }

                if (batch.stencilOp != UIStencilOp::None)
                    commandBuffer.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, batch.stencilRef);

                UIPushConstants pushConstants{};
                pushConstants.viewportSize = viewportSize;
                pushConstants.alphaThreshold = batch.alphaThreshold;
                pushConstants.flags = (batch.stencilOp == UIStencilOp::Write) ? 1u : 0u;

                commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                             0, sizeof(UIPushConstants), &pushConstants);
                commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, texDescSet, nullptr);
                commandBuffer.drawIndexed(6, batch.instanceCount, 0, 0, batch.firstInstance);
            }
        }

        core::endDynamicRendering(commandBuffer);

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            colorSrc[imageIndex].colorImage,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);
    }
}
