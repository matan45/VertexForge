#include "UITextPipeline.hpp"
#include "UIRenderTypes.hpp"
#include "../text/TextFontCache.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"

namespace render::ui
{
    void UITextPipeline::createDescriptorSetLayout()
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

    void UITextPipeline::createDescriptorPool()
    {
        uint32_t totalSets = 1 + MAX_FONT_DESCRIPTORS;

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

    void UITextPipeline::createDefaultDescriptorSet()
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        defaultDescriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        updateDescriptorSet(defaultDescriptorSet,
                            fontCache.getDefaultImageView(),
                            fontCache.getDefaultSampler());
    }

    void UITextPipeline::updateDescriptorSet(vk::DescriptorSet dstSet,
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

    vk::DescriptorSet UITextPipeline::getOrCreateFontDescriptorSet(const std::string& fontPath)
    {
        auto it = fontDescriptorSets.find(fontPath);
        if (it != fontDescriptorSets.end())
        {
            return it->second;
        }

        const render::text::CachedFont* cached = fontCache.getFont(fontPath);
        if (!cached)
        {
            return defaultDescriptorSet;
        }

        if (fontDescriptorSets.size() >= MAX_FONT_DESCRIPTORS)
        {
            vfLogWarning("UI text font descriptor limit reached ({})", MAX_FONT_DESCRIPTORS);
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

    void UITextPipeline::createPipeline()
    {
        auto vertexBinding = UIVertex::getBindingDescription();
        auto instanceBinding = UITextCharInstance::getBindingDescription();

        auto vertexAttribs = UIVertex::getAttributeDescriptions();
        auto instanceAttribs = UITextCharInstance::getAttributeDescriptions();

        std::vector<vk::VertexInputAttributeDescription> allAttribs;
        allAttribs.insert(allAttribs.end(), vertexAttribs.begin(), vertexAttribs.end());
        allAttribs.insert(allAttribs.end(), instanceAttribs.begin(), instanceAttribs.end());

        core::GraphicsPipelineConfig config{
            .device = device.getLogicalDevice(),
            .extent = swapChain.getDisplayExtent(),
            .colorAttachmentFormats = { swapChain.getSceneColorFormat() },
            .stencilAttachmentFormat = vk::Format::eS8Uint,
            .shaderStages = uiTextShader->getShaderStages(),
            .vertexBindings = {vertexBinding, instanceBinding},
            .vertexAttributes = allAttribs,
            .topology = vk::PrimitiveTopology::eTriangleList,
            .descriptorSetLayouts = {descriptorSetLayout},
            .pushConstantSize = sizeof(UITextPushConstants),
            .pushConstantStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .cullMode = vk::CullModeFlagBits::eNone,
            .depthTestEnable = false,
            .depthWriteEnable = false,
            .blendEnable = true,
            // VK-1435: dynamic viewport so this pipeline records into the main swapchain target
            // AND the UI Layer Builder's offscreen target at its reference resolution (the main
            // path sets the display extent at record time, so it stays behavior-preserving).
            .dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor }
        };

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        graphicsPipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;

        // Stencil test pipeline for text under masks
        vk::StencilOpState stencilTestOp{};
        stencilTestOp.failOp = vk::StencilOp::eKeep;
        stencilTestOp.passOp = vk::StencilOp::eKeep;
        stencilTestOp.depthFailOp = vk::StencilOp::eKeep;
        stencilTestOp.compareOp = vk::CompareOp::eLessOrEqual;
        stencilTestOp.compareMask = 0xFF;
        stencilTestOp.writeMask = 0x00;
        stencilTestOp.reference = 0;

        core::GraphicsPipelineConfig stencilConfig = config;
        stencilConfig.existingPipelineLayout = pipelineLayout;
        stencilConfig.stencilTestEnable = true;
        stencilConfig.stencilFront = stencilTestOp;
        stencilConfig.stencilBack = stencilTestOp;
        stencilConfig.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eStencilReference };

        auto stencilResult = core::PipelineUtilities::createGraphicsPipeline(stencilConfig);
        pipelineStencilTest = stencilResult.pipeline;
    }
}
