#include "UIRenderPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Texture.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "resource/Types.hpp"
#include "print/Log.hpp"
#include <filesystem>
#include <algorithm>
#include <string_view>

namespace render::ui
{
    UIRenderPipeline::UIRenderPipeline(core::Device& device, core::SwapChain& swapChain,
                                       core::OffscreenResources& offscreenResources)
        : device{device}
        , swapChain{swapChain}
        , offscreenResources{offscreenResources}
        , bufferManager{device}
    {
    }

    UIRenderPipeline::~UIRenderPipeline() = default;

    void UIRenderPipeline::init()
    {
        loadShader();
        createRenderPass();
        createDescriptorSetLayout();
        createDescriptorPool();

        bufferManager.init();

        createDefaultDescriptorSet();

        // Create 1x1 white fallback texture for color-only quads (scrollbars, etc.)
        {
            resource::TextureData whiteTexData;
            whiteTexData.width = 1;
            whiteTexData.height = 1;
            whiteTexData.numbersOfChannels = 4;
            whiteTexData.mipLevels = 1;
            whiteTexData.mipData.push_back({1, 1, {255, 255, 255, 255}});

            auto texture = std::make_unique<core::Texture>(device);
            texture->loadTextureFromData(whiteTexData, vk::Format::eR8G8B8A8Unorm, false);

            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &descriptorSetLayout;
            vk::DescriptorSet descSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
            updateDescriptorSet(descSet, texture->getImageView(), texture->getSampler());

            TextureEntry entry;
            entry.texture = std::move(texture);
            entry.descriptorSet = descSet;
            textureCache.emplace("__white_1x1__", std::move(entry));
        }

        createPipeline();
        createFramebuffers();

        initialized = true;
    }

    void UIRenderPipeline::loadShader()
    {
        uiShader = std::make_shared<core::Shader>(device);
        uiShader->readShader("../../resources/shaders/ui/ui_image.glsl");
    }

    void UIRenderPipeline::recreate()
    {
        for (auto& framebuffer : framebuffers)
        {
            device.getLogicalDevice().destroyFramebuffer(framebuffer);
        }
        device.getLogicalDevice().destroyRenderPass(renderPass);
        device.getLogicalDevice().destroyPipeline(graphicsPipeline);
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);

        createRenderPass();
        createPipeline();
        createFramebuffers();
    }

    void UIRenderPipeline::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

        for (auto& framebuffer : framebuffers)
        {
            dev.destroyFramebuffer(framebuffer);
        }
        framebuffers.clear();

        if (graphicsPipeline) dev.destroyPipeline(graphicsPipeline);
        if (pipelineLayout) dev.destroyPipelineLayout(pipelineLayout);

        textureCache.clear();
        externalTextureCache.clear();
        scissorGroups.clear();
        totalInstanceCount = 0;

        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (descriptorSetLayout) dev.destroyDescriptorSetLayout(descriptorSetLayout);

        if (renderPass)
            dev.destroyRenderPass(renderPass);

        bufferManager.cleanUp();

        if (uiShader)
        {
            uiShader->cleanUp();
            uiShader.reset();
        }

        initialized = false;
    }

    void UIRenderPipeline::createRenderPass()
    {
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = swapChain.getSwapchainImageFormat();
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = nullptr;

        std::array<vk::AttachmentDescription, 1> attachments = {colorAttachment};

        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        renderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);
    }

    void UIRenderPipeline::createDescriptorSetLayout()
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings(1);

        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[0].pImmutableSamplers = nullptr;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void UIRenderPipeline::createDescriptorPool()
    {
        uint32_t totalSets = 1 + MAX_UI_TEXTURES + MAX_EXTERNAL_TEXTURES;

        std::vector<vk::DescriptorPoolSize> poolSizes(1);
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = totalSets;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = totalSets;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void UIRenderPipeline::createDefaultDescriptorSet()
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        defaultDescriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
    }

    void UIRenderPipeline::updateDescriptorSet(vk::DescriptorSet dstSet,
                                                vk::ImageView imageView, vk::Sampler sampler)
    {
        vk::DescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfo.imageView = imageView;
        imageInfo.sampler = sampler;

        vk::WriteDescriptorSet imageWrite{};
        imageWrite.dstSet = dstSet;
        imageWrite.dstBinding = 0;
        imageWrite.dstArrayElement = 0;
        imageWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        imageWrite.descriptorCount = 1;
        imageWrite.pImageInfo = &imageInfo;

        device.getLogicalDevice().updateDescriptorSets(imageWrite, nullptr);
    }

    void UIRenderPipeline::createPipeline()
    {
        auto vertexBinding = UIVertex::getBindingDescription();
        auto instanceBinding = UIImageInstance::getBindingDescription();

        auto vertexAttribs = UIVertex::getAttributeDescriptions();
        auto instanceAttribs = UIImageInstance::getAttributeDescriptions();

        std::vector<vk::VertexInputAttributeDescription> allAttribs;
        allAttribs.insert(allAttribs.end(), vertexAttribs.begin(), vertexAttribs.end());
        allAttribs.insert(allAttribs.end(), instanceAttribs.begin(), instanceAttribs.end());

        core::GraphicsPipelineConfig config{
            .device = device.getLogicalDevice(),
            .renderPass = renderPass,
            .extent = swapChain.getSwapchainExtent(),
            .shaderStages = uiShader->getShaderStages(),
            .vertexBindings = {vertexBinding, instanceBinding},
            .vertexAttributes = std::move(allAttribs),
            .topology = vk::PrimitiveTopology::eTriangleList,
            .descriptorSetLayouts = {descriptorSetLayout},
            .pushConstantSize = sizeof(UIPushConstants),
            .pushConstantStages = vk::ShaderStageFlagBits::eVertex,
            .cullMode = vk::CullModeFlagBits::eNone,
            .depthTestEnable = false,
            .depthWriteEnable = false,
            .blendEnable = true,
            .dynamicStates = { vk::DynamicState::eScissor }
        };

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        graphicsPipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;
    }

    void UIRenderPipeline::createFramebuffers()
    {
        framebuffers.resize(offscreenResources.colorImages.size());

        for (uint32_t i = 0; i < framebuffers.size(); i++)
        {
            vk::ImageView colorView = offscreenResources.colorImages[i].colorImageView;
            std::array<vk::ImageView, 1> attachments = {colorView};

            vk::FramebufferCreateInfo framebufferInfo{};
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = swapChain.getSwapchainExtent().width;
            framebufferInfo.height = swapChain.getSwapchainExtent().height;
            framebufferInfo.layers = 1;

            framebuffers[i] = device.getLogicalDevice().createFramebuffer(framebufferInfo);
        }
    }

    bool UIRenderPipeline::loadTexture(const std::string& texturePath)
    {
        if (textureCache.contains(texturePath))
        {
            return true;
        }

        if (textureCache.size() >= MAX_UI_TEXTURES)
        {
            vfLogWarning("UI texture limit reached ({}), cannot load: {}",
                          MAX_UI_TEXTURES, texturePath);
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
        if (!initialized || !imageView || !externalSampler)
            return;

        auto it = externalTextureCache.find(key);
        if (it != externalTextureCache.end())
        {
            updateDescriptorSet(it->second, imageView, externalSampler);
            return;
        }

        if (externalTextureCache.size() >= MAX_EXTERNAL_TEXTURES)
            return;

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
        {
            device.getLogicalDevice().freeDescriptorSets(descriptorPool, descSet);
        }
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

        // Group images by scissor rect, then by texture within each group
        struct ScissorKey
        {
            int32_t x, y, w, h;
            bool operator==(const ScissorKey& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
        };
        struct ScissorKeyHash
        {
            size_t operator()(const ScissorKey& k) const
            {
                size_t h = std::hash<int32_t>{}(k.x);
                h ^= std::hash<int32_t>{}(k.y) << 1;
                h ^= std::hash<int32_t>{}(k.w) << 2;
                h ^= std::hash<int32_t>{}(k.h) << 3;
                return h;
            }
        };

        struct ImageEntry
        {
            std::string_view texturePath;
            UIImageInstance instance;
        };

        std::unordered_map<ScissorKey, std::vector<ImageEntry>, ScissorKeyHash> scissorMap;

        for (const auto& image : images)
        {
            if (image.texturePath.empty())
            {
                continue;
            }

            ScissorKey key{
                static_cast<int32_t>(image.scissorRect.x),
                static_cast<int32_t>(image.scissorRect.y),
                static_cast<int32_t>(image.scissorRect.z),
                static_cast<int32_t>(image.scissorRect.w)
            };

            UIImageInstance inst{};
            inst.posAndSize = glm::vec4(image.position, image.size);
            inst.colorTint = image.colorTint;
            inst.uvRect = image.uvRect;

            scissorMap[key].push_back({image.texturePath, inst});
        }

        // Build ordered instance buffer and scissor groups
        std::vector<UIImageInstance> allInstances;
        allInstances.reserve(images.size());

        for (auto& [key, entries] : scissorMap)
        {
            UIScissorGroup group;
            group.scissorRect = glm::vec4(key.x, key.y, key.w, key.h);

            // Sub-group by texture within this scissor group
            std::unordered_map<std::string_view, std::vector<UIImageInstance>> texturedInstances;
            for (auto& entry : entries)
            {
                texturedInstances[entry.texturePath].push_back(entry.instance);
            }

            for (auto& [path, instances] : texturedInstances)
            {
                std::string pathStr(path);
                // RTT synthetic keys are only in externalTextureCache, never file-loaded
                bool isRTTSynthetic = pathStr.starts_with("__rtt_");
                bool hasTexture = externalTextureCache.contains(pathStr) || (!isRTTSynthetic && loadTexture(pathStr));
                if (!hasTexture)
                {
                    continue;
                }

                UITextureBatch batch;
                batch.texturePath = std::string(path);
                batch.firstInstance = static_cast<uint32_t>(allInstances.size());
                batch.instanceCount = static_cast<uint32_t>(instances.size());
                group.batches.push_back(std::move(batch));

                allInstances.insert(allInstances.end(), instances.begin(), instances.end());
            }

            if (!group.batches.empty())
            {
                scissorGroups.push_back(std::move(group));
            }
        }

        totalInstanceCount = static_cast<uint32_t>(allInstances.size());
        bufferManager.updateInstanceBuffer(allInstances);
    }

    void UIRenderPipeline::recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                                uint32_t imageIndex) const
    {
        if (!initialized || totalInstanceCount == 0)
        {
            return;
        }

        vk::RenderPassBeginInfo renderPassInfo{};
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[imageIndex];
        renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderPassInfo.renderArea.extent = swapChain.getSwapchainExtent();

        commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        vk::Buffer vertexBuffers[] = {bufferManager.getQuadVertexBuffer(), bufferManager.getInstanceBuffer()};
        vk::DeviceSize offsets[] = {0, 0};
        commandBuffer.bindVertexBuffers(0, 2, vertexBuffers, offsets);
        commandBuffer.bindIndexBuffer(bufferManager.getQuadIndexBuffer(), 0, vk::IndexType::eUint16);

        glm::vec2 viewportSize(
            static_cast<float>(swapChain.getSwapchainExtent().width),
            static_cast<float>(swapChain.getSwapchainExtent().height)
        );

        UIPushConstants pushConstants{};
        pushConstants.viewportSize = viewportSize;

        commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eVertex,
                                     0, sizeof(UIPushConstants), &pushConstants);

        for (const auto& group : scissorGroups)
        {
            // Set scissor for this group
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
                // No scissor - full viewport
                scissor.offset = vk::Offset2D{0, 0};
                scissor.extent = swapChain.getSwapchainExtent();
            }
            commandBuffer.setScissor(0, 1, &scissor);

            for (const auto& batch : group.batches)
            {
                vk::DescriptorSet texDescSet;
                auto it = textureCache.find(batch.texturePath);
                if (it != textureCache.end())
                {
                    texDescSet = it->second.descriptorSet;
                }
                else
                {
                    auto extIt = externalTextureCache.find(batch.texturePath);
                    if (extIt != externalTextureCache.end())
                    {
                        texDescSet = extIt->second;
                    }
                    else
                    {
                        continue;
                    }
                }

                commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                                  0, texDescSet, nullptr);

                commandBuffer.drawIndexed(6, batch.instanceCount, 0, 0, batch.firstInstance);
            }
        }

        commandBuffer.endRenderPass();
    }
}
