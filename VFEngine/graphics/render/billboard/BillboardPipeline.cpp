#include "BillboardPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Texture.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "print/Logger.hpp"
#include <filesystem>
#include <algorithm>

namespace render::billboard
{
    BillboardPipeline::BillboardPipeline(core::Device& device, core::SwapChain& swapChain,
                                         core::OffscreenResources& offscreenResources)
        : device{device}
        , swapChain{swapChain}
        , offscreenResources{offscreenResources}
        , bufferManager{device}
        , atlasManager{device}
    {
    }

    BillboardPipeline::~BillboardPipeline() = default;

    void BillboardPipeline::init()
    {
        loadShader();
        createRenderPass();
        createDescriptorSetLayout();
        createDescriptorPool();

        bufferManager.init();
        atlasManager.init();

        createDescriptorSet();
        createPipeline();
        createFramebuffers();

        initialized = true;
    }

    void BillboardPipeline::loadShader()
    {
        billboardShader = std::make_shared<core::Shader>(device);
        billboardShader->readShader("../../resources/shaders/billboard/billboard.glsl");
    }

    void BillboardPipeline::recreate()
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

    void BillboardPipeline::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

        for (auto& framebuffer : framebuffers)
        {
            dev.destroyFramebuffer(framebuffer);
        }
        framebuffers.clear();

        if (graphicsPipeline) dev.destroyPipeline(graphicsPipeline);
        if (pipelineLayout) dev.destroyPipelineLayout(pipelineLayout);

        customTextureCache.clear();
        externalTextureCache.clear();
        customBatches.clear();
        atlasInstanceCount = 0;

        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (descriptorSetLayout) dev.destroyDescriptorSetLayout(descriptorSetLayout);

        if (renderPass)
            dev.destroyRenderPass(renderPass);

        bufferManager.cleanUp();
        atlasManager.cleanUp();

        if (billboardShader)
        {
            billboardShader->cleanUp();
            billboardShader.reset();
        }

        initialized = false;
    }

    void BillboardPipeline::createRenderPass()
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

        vk::AttachmentDescription depthAttachment{};
        depthAttachment.format = swapChain.getSwapchainDepthStencilFormat();
        depthAttachment.samples = vk::SampleCountFlagBits::e1;
        depthAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
        depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        std::array<vk::AttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        renderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);
    }

    void BillboardPipeline::createDescriptorSetLayout()
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings(2);

        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex;
        bindings[0].pImmutableSamplers = nullptr;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[1].pImmutableSamplers = nullptr;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void BillboardPipeline::createDescriptorPool()
    {
        uint32_t totalSets = 1 + MAX_CUSTOM_TEXTURES + MAX_EXTERNAL_TEXTURES;

        std::vector<vk::DescriptorPoolSize> poolSizes(2);
        poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = totalSets;
        poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = totalSets;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = totalSets;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void BillboardPipeline::createDescriptorSet()
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        atlasDescriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        updateDescriptorSet(atlasDescriptorSet, atlasManager.getImageView(), atlasManager.getSampler());
    }

    void BillboardPipeline::updateDescriptorSet(vk::DescriptorSet dstSet,
                                                  vk::ImageView imageView, vk::Sampler sampler)
    {
        vk::DescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = bufferManager.getCameraUBO();
        uboBufferInfo.offset = 0;
        uboBufferInfo.range = sizeof(BillboardCameraUBO);

        vk::WriteDescriptorSet uboWrite{};
        uboWrite.dstSet = dstSet;
        uboWrite.dstBinding = 0;
        uboWrite.dstArrayElement = 0;
        uboWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        uboWrite.descriptorCount = 1;
        uboWrite.pBufferInfo = &uboBufferInfo;

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfo.imageView = imageView;
        imageInfo.sampler = sampler;

        vk::WriteDescriptorSet imageWrite{};
        imageWrite.dstSet = dstSet;
        imageWrite.dstBinding = 1;
        imageWrite.dstArrayElement = 0;
        imageWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        imageWrite.descriptorCount = 1;
        imageWrite.pImageInfo = &imageInfo;

        std::array<vk::WriteDescriptorSet, 2> descriptorWrites = {uboWrite, imageWrite};
        device.getLogicalDevice().updateDescriptorSets(descriptorWrites, nullptr);
    }

    void BillboardPipeline::createPipeline()
    {
        auto vertexBinding = BillboardVertex::getBindingDescription();
        auto instanceBinding = BillboardInstanceData::getBindingDescription();

        auto vertexAttribs = BillboardVertex::getAttributeDescriptions();
        auto instanceAttribs = BillboardInstanceData::getAttributeDescriptions();

        std::vector<vk::VertexInputAttributeDescription> allAttribs;
        allAttribs.insert(allAttribs.end(), vertexAttribs.begin(), vertexAttribs.end());
        allAttribs.insert(allAttribs.end(), instanceAttribs.begin(), instanceAttribs.end());

        core::GraphicsPipelineConfig config{
            .device = device.getLogicalDevice(),
            .renderPass = renderPass,
            .extent = swapChain.getSwapchainExtent(),
            .shaderStages = billboardShader->getShaderStages(),
            .vertexBindings = {vertexBinding, instanceBinding},
            .vertexAttributes = std::move(allAttribs),
            .topology = vk::PrimitiveTopology::eTriangleList,
            .descriptorSetLayouts = {descriptorSetLayout},
            .pushConstantSize = sizeof(BillboardPushConstants),
            .pushConstantStages = vk::ShaderStageFlagBits::eVertex,
            .cullMode = vk::CullModeFlagBits::eNone,
            .depthTestEnable = true,
            .depthWriteEnable = false,
            .blendEnable = true
        };

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        graphicsPipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;
    }

    void BillboardPipeline::createFramebuffers()
    {
        framebuffers.resize(offscreenResources.colorImages.size());
        vk::ImageView depth = offscreenResources.depthImage.depthImageView;

        for (uint32_t i = 0; i < framebuffers.size(); i++)
        {
            vk::ImageView colorView = offscreenResources.colorImages[i].colorImageView;
            std::array<vk::ImageView, 2> attachments = {colorView, depth};

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

    bool BillboardPipeline::loadAtlas(const std::string& atlasPath)
    {
        bool result = atlasManager.loadAtlas(atlasPath);
        if (result && atlasDescriptorSet)
        {
            updateDescriptorSet(atlasDescriptorSet, atlasManager.getImageView(), atlasManager.getSampler());
        }
        return result;
    }

    bool BillboardPipeline::loadCustomTexture(const std::string& texturePath)
    {
        if (customTextureCache.contains(texturePath))
        {
            return true;
        }

        if (customTextureCache.size() >= MAX_CUSTOM_TEXTURES)
        {
            loggerWarning("Billboard custom texture limit reached ({}), cannot load: {}",
                          MAX_CUSTOM_TEXTURES, texturePath);
            return false;
        }

        if (!std::filesystem::exists(texturePath))
        {
            loggerWarning("Billboard texture file not found: {}", texturePath);
            return false;
        }

        auto texture = std::make_unique<core::Texture>(device);
        texture->loadTextureFromFile(texturePath, vk::Format::eR8G8B8A8Unorm, false);

        if (!texture->getImageView())
        {
            loggerError("Failed to load billboard texture: {}", texturePath);
            return false;
        }

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        vk::DescriptorSet newDescSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
        updateDescriptorSet(newDescSet, texture->getImageView(), texture->getSampler());

        CustomTextureEntry entry;
        entry.texture = std::move(texture);
        entry.descriptorSet = newDescSet;
        customTextureCache.emplace(texturePath, std::move(entry));

        loggerInfo("Billboard custom texture loaded: {}", texturePath);
        return true;
    }

    void BillboardPipeline::registerExternalTexture(const std::string& key,
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

    void BillboardPipeline::unregisterExternalTexture(const std::string& key)
    {
        auto it = externalTextureCache.find(key);
        if (it != externalTextureCache.end())
        {
            device.getLogicalDevice().freeDescriptorSets(descriptorPool, it->second);
            externalTextureCache.erase(it);
        }
    }

    void BillboardPipeline::clearExternalTextures()
    {
        for (auto& [key, descSet] : externalTextureCache)
        {
            device.getLogicalDevice().freeDescriptorSets(descriptorPool, descSet);
        }
        externalTextureCache.clear();
    }

    void BillboardPipeline::updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                            const glm::vec3& cameraPos)
    {
        cameraPos_ = cameraPos;
        bufferManager.updateCameraUBO(view, projection, cameraPos);
    }

    void BillboardPipeline::setBillboardList(const std::vector<BillboardRenderData>& billboards)
    {
        customBatches.clear();
        atlasInstanceCount = 0;

        if (billboards.empty())
        {
            bufferManager.updateInstanceBuffer({});
            return;
        }

        std::vector<BillboardRenderData> atlasBillboards;
        std::unordered_map<std::string, std::vector<BillboardRenderData>> texturedBillboards;

        for (const auto& billboard : billboards)
        {
            if (distanceCullingEnabled_ && maxBillboardDistSq_ > 0.0f)
            {
                glm::vec3 diff = billboard.worldPosition - cameraPos_;
                float distSq = glm::dot(diff, diff);
                if (distSq > maxBillboardDistSq_)
                {
                    continue;
                }
            }
            if (billboard.texturePath.empty())
            {
                atlasBillboards.push_back(billboard);
            }
            else
            {
                texturedBillboards[billboard.texturePath].push_back(billboard);
            }
        }

        std::vector<BillboardRenderData> orderedBillboards;
        orderedBillboards.reserve(billboards.size());

        orderedBillboards.insert(orderedBillboards.end(), atlasBillboards.begin(), atlasBillboards.end());
        atlasInstanceCount = static_cast<uint32_t>(atlasBillboards.size());

        for (auto& [path, batchBillboards] : texturedBillboards)
        {
            bool hasTexture = externalTextureCache.contains(path) || loadCustomTexture(path);
            if (hasTexture)
            {
                CustomTextureBatch batch;
                batch.texturePath = path;
                batch.firstInstance = static_cast<uint32_t>(orderedBillboards.size());
                batch.instanceCount = static_cast<uint32_t>(batchBillboards.size());
                customBatches.push_back(std::move(batch));

                orderedBillboards.insert(orderedBillboards.end(),
                                         batchBillboards.begin(), batchBillboards.end());
            }
        }

        bufferManager.updateInstanceBuffer(orderedBillboards);
    }

    void BillboardPipeline::recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                                 uint32_t imageIndex) const
    {
        uint32_t totalInstances = atlasInstanceCount;
        for (const auto& batch : customBatches)
        {
            totalInstances += batch.instanceCount;
        }

        if (!initialized || totalInstances == 0)
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

        if (atlasInstanceCount > 0)
        {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                              0, atlasDescriptorSet, nullptr);

            BillboardPushConstants pushConstants{};
            pushConstants.viewportSize = viewportSize;
            pushConstants.atlasGridSize = static_cast<float>(AtlasConfig::GRID_SIZE);

            commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eVertex,
                                         0, sizeof(BillboardPushConstants), &pushConstants);

            commandBuffer.drawIndexed(6, atlasInstanceCount, 0, 0, 0);
        }

        for (const auto& batch : customBatches)
        {
            vk::DescriptorSet texDescSet;
            auto it = customTextureCache.find(batch.texturePath);
            if (it != customTextureCache.end())
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

            BillboardPushConstants pushConstants{};
            pushConstants.viewportSize = viewportSize;
            pushConstants.atlasGridSize = 1.0f;

            commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eVertex,
                                         0, sizeof(BillboardPushConstants), &pushConstants);

            commandBuffer.drawIndexed(6, batch.instanceCount, 0, 0, batch.firstInstance);
        }

        commandBuffer.endRenderPass();
    }
}
