#include "BillboardPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Texture.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "../../core/ImageUtilities.hpp"
#include "print/Log.hpp"
#include <filesystem>
#include <algorithm>
#include <cassert>

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
        createDescriptorSetLayout();
        createDescriptorPool();

        bufferManager.init();
        atlasManager.init();

        createDescriptorSet();
        createPipeline();

        initialized = true;
    }

    void BillboardPipeline::loadShader()
    {
        billboardShader = std::make_shared<core::Shader>(device);
        billboardShader->readShader("../../resources/shaders/billboard/billboard.glsl");
    }

    void BillboardPipeline::recreate()
    {
        device.getLogicalDevice().destroyPipeline(graphicsPipeline);
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);

        createPipeline();
    }

    void BillboardPipeline::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

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

        bufferManager.cleanUp();
        atlasManager.cleanUp();

        if (billboardShader)
        {
            billboardShader->cleanUp();
            billboardShader.reset();
        }

        initialized = false;
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
        uint32_t totalSets = 1 + MAX_CUSTOM_TEXTURES + (MAX_EXTERNAL_TEXTURES * swapChain.getImageCount());

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
            .extent = swapChain.getSwapchainExtent(),
            .colorAttachmentFormats = { swapChain.getSceneColorFormat() },
            .depthAttachmentFormat = swapChain.getSwapchainDepthStencilFormat(),
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
            .blendEnable = true,
        };

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        graphicsPipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;
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
            vfLogWarning("Billboard custom texture limit reached ({}), cannot load: {}",
                          MAX_CUSTOM_TEXTURES, texturePath);
            return false;
        }

        if (!std::filesystem::exists(texturePath))
        {
            vfLogWarning("Billboard texture file not found: {}", texturePath);
            return false;
        }

        auto texture = std::make_unique<core::Texture>(device);
        texture->loadTextureFromFile(texturePath, vk::Format::eR8G8B8A8Unorm, false);

        if (!texture->getImageView())
        {
            vfLogError("Failed to load billboard texture: {}", texturePath);
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

        vfLogInfo("Billboard custom texture loaded: {}", texturePath);
        return true;
    }

    void BillboardPipeline::registerExternalTexture(const std::string& key, uint32_t imageIndex,
                                                       vk::ImageView imageView, vk::Sampler externalSampler)
    {
        if (!initialized || !imageView || !externalSampler)
            return;
        if (imageIndex >= swapChain.getImageCount())
            return;

        auto it = externalTextureCache.find(key);
        if (it != externalTextureCache.end())
        {
            auto& entry = it->second;
            if (imageIndex >= entry.descriptorSets.size())
                return;

            if (!entry.descriptorSets[imageIndex])
            {
                vk::DescriptorSetAllocateInfo allocInfo{};
                allocInfo.descriptorPool = descriptorPool;
                allocInfo.descriptorSetCount = 1;
                allocInfo.pSetLayouts = &descriptorSetLayout;

                entry.descriptorSets[imageIndex] = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
            }

            if (entry.imageViews[imageIndex] != imageView || entry.samplers[imageIndex] != externalSampler)
            {
                updateDescriptorSet(entry.descriptorSets[imageIndex], imageView, externalSampler);
                entry.imageViews[imageIndex] = imageView;
                entry.samplers[imageIndex] = externalSampler;
            }
            return;
        }

        if (externalTextureCache.size() >= MAX_EXTERNAL_TEXTURES)
            return;

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        ExternalTextureEntry entry;
        entry.descriptorSets.resize(swapChain.getImageCount());
        entry.imageViews.resize(swapChain.getImageCount());
        entry.samplers.resize(swapChain.getImageCount());
        entry.descriptorSets[imageIndex] = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
        updateDescriptorSet(entry.descriptorSets[imageIndex], imageView, externalSampler);
        entry.imageViews[imageIndex] = imageView;
        entry.samplers[imageIndex] = externalSampler;
        externalTextureCache[key] = std::move(entry);
    }

    void BillboardPipeline::unregisterExternalTexture(const std::string& key)
    {
        auto it = externalTextureCache.find(key);
        if (it != externalTextureCache.end())
        {
            for (auto descSet : it->second.descriptorSets)
            {
                if (descSet)
                    device.getLogicalDevice().freeDescriptorSets(descriptorPool, descSet);
            }
            externalTextureCache.erase(it);
        }
    }

    void BillboardPipeline::clearExternalTextures()
    {
        for (auto& [key, entry] : externalTextureCache)
        {
            for (auto descSet : entry.descriptorSets)
            {
                if (descSet)
                    device.getLogicalDevice().freeDescriptorSets(descriptorPool, descSet);
            }
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
            // RTT synthetic keys are only in externalTextureCache, never file-loaded
            bool isRTTSynthetic = path.starts_with("__rtt_");
            bool hasTexture = externalTextureCache.contains(path) || (!isRTTSynthetic && loadCustomTexture(path));
            if (hasTexture)
            {
                CustomTextureBatch batch;
                batch.texturePath = path;
                batch.firstInstance = static_cast<uint32_t>(orderedBillboards.size());
                batch.instanceCount = static_cast<uint32_t>(batchBillboards.size());
                batch.atlasGridSize = batchBillboards.front().atlasGridSize;
#ifndef NDEBUG
                for (const auto& bb : batchBillboards)
                {
                    assert(bb.atlasGridSize == batch.atlasGridSize &&
                           "All billboards in a texture batch must share the same atlasGridSize");
                }
#endif
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

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.colorImages[imageIndex].colorImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        vk::ImageView colorView = offscreenResources.colorImages[imageIndex].colorImageView;
        vk::ImageView depthView = offscreenResources.depthImage.depthImageView;

        core::DynamicRenderingInfo info{};
        info.extent = swapChain.getSwapchainExtent();
        info.colorAttachments = { core::colorLoad(colorView) };
        info.depthAttachment = core::depthLoad(depthView);

        core::beginDynamicRendering(commandBuffer, info);

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
            render::FrameDrawStats::count();
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
                if (extIt != externalTextureCache.end() &&
                    imageIndex < extIt->second.descriptorSets.size() &&
                    extIt->second.descriptorSets[imageIndex])
                {
                    texDescSet = extIt->second.descriptorSets[imageIndex];
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
            pushConstants.atlasGridSize = batch.atlasGridSize;

            commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eVertex,
                                         0, sizeof(BillboardPushConstants), &pushConstants);

            commandBuffer.drawIndexed(6, batch.instanceCount, 0, 0, batch.firstInstance);
            render::FrameDrawStats::count();
        }

        core::endDynamicRendering(commandBuffer);

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.colorImages[imageIndex].colorImage,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);
    }

    void BillboardPipeline::recordCommandBufferGraphManaged(const vk::CommandBuffer& commandBuffer,
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

        vk::ImageView colorView = offscreenResources.colorImages[imageIndex].colorImageView;
        vk::ImageView depthView = offscreenResources.depthImage.depthImageView;

        core::DynamicRenderingInfo info{};
        info.extent = swapChain.getSwapchainExtent();
        info.colorAttachments = { core::colorLoad(colorView) };
        info.depthAttachment = core::depthLoad(depthView);

        core::beginDynamicRendering(commandBuffer, info);

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
            render::FrameDrawStats::count();
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
                if (extIt != externalTextureCache.end() &&
                    imageIndex < extIt->second.descriptorSets.size() &&
                    extIt->second.descriptorSets[imageIndex])
                {
                    texDescSet = extIt->second.descriptorSets[imageIndex];
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
            pushConstants.atlasGridSize = batch.atlasGridSize;

            commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eVertex,
                                         0, sizeof(BillboardPushConstants), &pushConstants);

            commandBuffer.drawIndexed(6, batch.instanceCount, 0, 0, batch.firstInstance);
            render::FrameDrawStats::count();
        }

        core::endDynamicRendering(commandBuffer);
    }
}
