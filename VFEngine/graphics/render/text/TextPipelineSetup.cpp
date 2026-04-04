#include "TextPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"

namespace render::text
{
    void TextPipeline::createDescriptorSetLayout()
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings(2);

        // Binding 0: Camera UBO
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex;
        bindings[0].pImmutableSamplers = nullptr;

        // Binding 1: Font atlas sampler
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

    void TextPipeline::createDescriptorPool()
    {
        uint32_t totalSets = 1 + MAX_FONT_DESCRIPTORS;

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

    void TextPipeline::createDefaultDescriptorSet()
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        defaultDescriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        updateDescriptorSet(defaultDescriptorSet, fontCache.getDefaultImageView(), fontCache.getDefaultSampler());
    }

    void TextPipeline::updateDescriptorSet(vk::DescriptorSet dstSet,
                                            vk::ImageView imageView, vk::Sampler sampler)
    {
        vk::DescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = bufferManager.getCameraUBO();
        uboBufferInfo.offset = 0;
        uboBufferInfo.range = sizeof(TextCameraUBO);

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

    vk::DescriptorSet TextPipeline::getOrCreateFontDescriptorSet(const std::string& fontPath)
    {
        auto it = fontDescriptorSets.find(fontPath);
        if (it != fontDescriptorSets.end())
        {
            return it->second;
        }

        const CachedFont* cached = fontCache.getFont(fontPath);
        if (!cached)
        {
            return defaultDescriptorSet;
        }

        if (fontDescriptorSets.size() >= MAX_FONT_DESCRIPTORS)
        {
            vfLogWarning("Text font descriptor limit reached ({})", MAX_FONT_DESCRIPTORS);
            return defaultDescriptorSet;
        }

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        vk::DescriptorSet newSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
        updateDescriptorSet(newSet, cached->atlasImageView, cached->atlasSampler);

        fontDescriptorSets.emplace(fontPath, newSet);
        return newSet;
    }

    void TextPipeline::createPipeline()
    {
        auto vertexBinding = TextVertex::getBindingDescription();
        auto instanceBinding = TextCharInstance::getBindingDescription();

        auto vertexAttribs = TextVertex::getAttributeDescriptions();
        auto instanceAttribs = TextCharInstance::getAttributeDescriptions();

        std::vector<vk::VertexInputAttributeDescription> allAttribs;
        allAttribs.insert(allAttribs.end(), vertexAttribs.begin(), vertexAttribs.end());
        allAttribs.insert(allAttribs.end(), instanceAttribs.begin(), instanceAttribs.end());

        core::GraphicsPipelineConfig config{
            .device = device.getLogicalDevice(),
            .renderPass = nullptr,
            .extent = swapChain.getSwapchainExtent(),
            .colorAttachmentFormats = {swapChain.getSceneColorFormat()},
            .depthAttachmentFormat = swapChain.getSwapchainDepthStencilFormat(),
            .shaderStages = textShader->getShaderStages(),
            .vertexBindings = {vertexBinding, instanceBinding},
            .vertexAttributes = std::move(allAttribs),
            .topology = vk::PrimitiveTopology::eTriangleList,
            .descriptorSetLayouts = {descriptorSetLayout},
            .pushConstantSize = sizeof(TextPushConstants),
            .pushConstantStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .cullMode = vk::CullModeFlagBits::eNone,
            .depthTestEnable = true,
            .depthWriteEnable = false,
            .blendEnable = true
        };

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        graphicsPipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;
    }
}
