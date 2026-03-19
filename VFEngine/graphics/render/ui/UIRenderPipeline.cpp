#include "UIRenderPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Texture.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "resource/Types.hpp"
#include "print/Log.hpp"

namespace render::ui
{
    UIRenderPipeline::UIRenderPipeline(core::Device& device, core::SwapChain& swapChain,
                                       core::OffscreenResources& offscreenResources)
        : device{device}, swapChain{swapChain}, offscreenResources{offscreenResources}, bufferManager{device}
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

        {
            resource::TextureData whiteTexData;
            whiteTexData.width = 1; whiteTexData.height = 1; whiteTexData.numbersOfChannels = 4; whiteTexData.mipLevels = 1;
            whiteTexData.mipData.push_back({.width = 1, .height = 1, .dataSize = 4, .data = {255, 255, 255, 255}});

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
        for (auto& framebuffer : framebuffers) device.getLogicalDevice().destroyFramebuffer(framebuffer);
        device.getLogicalDevice().destroyRenderPass(renderPass);

        auto& dev = device.getLogicalDevice();
        if (pipelineNormal) dev.destroyPipeline(pipelineNormal);
        if (pipelineStencilIncNoColor) dev.destroyPipeline(pipelineStencilIncNoColor);
        if (pipelineStencilIncColor) dev.destroyPipeline(pipelineStencilIncColor);
        if (pipelineStencilTest) dev.destroyPipeline(pipelineStencilTest);
        if (pipelineStencilDecNoColor) dev.destroyPipeline(pipelineStencilDecNoColor);
        dev.destroyPipelineLayout(pipelineLayout);

        createRenderPass();
        createPipeline();
        createFramebuffers();
    }

    void UIRenderPipeline::cleanUp()
    {
        auto& dev = device.getLogicalDevice();
        for (auto& framebuffer : framebuffers) dev.destroyFramebuffer(framebuffer);
        framebuffers.clear();

        if (pipelineNormal) dev.destroyPipeline(pipelineNormal);
        if (pipelineStencilIncNoColor) dev.destroyPipeline(pipelineStencilIncNoColor);
        if (pipelineStencilIncColor) dev.destroyPipeline(pipelineStencilIncColor);
        if (pipelineStencilTest) dev.destroyPipeline(pipelineStencilTest);
        if (pipelineStencilDecNoColor) dev.destroyPipeline(pipelineStencilDecNoColor);
        if (pipelineLayout) dev.destroyPipelineLayout(pipelineLayout);

        textureCache.clear();
        externalTextureCache.clear();
        scissorGroups.clear();
        totalInstanceCount = 0;

        if (descriptorPool) { dev.destroyDescriptorPool(descriptorPool); descriptorPool = nullptr; }
        if (descriptorSetLayout) dev.destroyDescriptorSetLayout(descriptorSetLayout);
        if (renderPass) dev.destroyRenderPass(renderPass);
        bufferManager.cleanUp();
        if (uiShader) { uiShader->cleanUp(); uiShader.reset(); }
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

        vk::AttachmentDescription stencilAttachment{};
        stencilAttachment.format = vk::Format::eS8Uint;
        stencilAttachment.samples = vk::SampleCountFlagBits::e1;
        stencilAttachment.loadOp = vk::AttachmentLoadOp::eDontCare;
        stencilAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
        stencilAttachment.stencilLoadOp = vk::AttachmentLoadOp::eClear;
        stencilAttachment.stencilStoreOp = vk::AttachmentStoreOp::eStore;
        stencilAttachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        stencilAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference colorRef{0, vk::ImageLayout::eColorAttachmentOptimal};
        vk::AttachmentReference stencilRef{1, vk::ImageLayout::eDepthStencilAttachmentOptimal};

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &stencilRef;

        std::array<vk::AttachmentDescription, 2> attachments = {colorAttachment, stencilAttachment};

        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        renderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);
    }

    void UIRenderPipeline::createDescriptorSetLayout()
    {
        vk::DescriptorSetLayoutBinding binding{0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment};
        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void UIRenderPipeline::createDescriptorPool()
    {
        uint32_t totalSets = 1 + MAX_UI_TEXTURES + MAX_EXTERNAL_TEXTURES;
        vk::DescriptorPoolSize poolSize{vk::DescriptorType::eCombinedImageSampler, totalSets};
        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
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

    void UIRenderPipeline::updateDescriptorSet(vk::DescriptorSet dstSet, vk::ImageView imageView, vk::Sampler sampler)
    {
        vk::DescriptorImageInfo imageInfo{sampler, imageView, vk::ImageLayout::eShaderReadOnlyOptimal};
        vk::WriteDescriptorSet imageWrite{};
        imageWrite.dstSet = dstSet;
        imageWrite.dstBinding = 0;
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
            .device = device.getLogicalDevice(), .renderPass = renderPass,
            .extent = swapChain.getSwapchainExtent(), .shaderStages = uiShader->getShaderStages(),
            .vertexBindings = {vertexBinding, instanceBinding}, .vertexAttributes = allAttribs,
            .topology = vk::PrimitiveTopology::eTriangleList, .descriptorSetLayouts = {descriptorSetLayout},
            .pushConstantSize = sizeof(UIPushConstants),
            .pushConstantStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .cullMode = vk::CullModeFlagBits::eNone, .depthTestEnable = false, .depthWriteEnable = false,
            .blendEnable = true, .dynamicStates = { vk::DynamicState::eScissor }
        };

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        pipelineNormal = result.pipeline;
        pipelineLayout = result.pipelineLayout;

        vk::StencilOpState stencilIncOp{vk::StencilOp::eKeep, vk::StencilOp::eIncrementAndClamp,
            vk::StencilOp::eKeep, vk::CompareOp::eAlways, 0xFF, 0xFF, 0};
        vk::StencilOpState stencilTestOp{vk::StencilOp::eKeep, vk::StencilOp::eKeep,
            vk::StencilOp::eKeep, vk::CompareOp::eLessOrEqual, 0xFF, 0x00, 0};
        vk::StencilOpState stencilDecOp{vk::StencilOp::eKeep, vk::StencilOp::eDecrementAndClamp,
            vk::StencilOp::eKeep, vk::CompareOp::eAlways, 0xFF, 0xFF, 0};

        std::vector<vk::DynamicState> stencilDynStates = {vk::DynamicState::eScissor, vk::DynamicState::eStencilReference};

        auto makeStencilPipeline = [&](vk::StencilOpState op, bool colorWrite) -> vk::Pipeline {
            core::GraphicsPipelineConfig cfg = config;
            cfg.existingPipelineLayout = pipelineLayout;
            cfg.stencilTestEnable = true;
            cfg.stencilFront = op; cfg.stencilBack = op;
            if (!colorWrite) cfg.colorWriteMask = {};
            cfg.dynamicStates = stencilDynStates;
            return core::PipelineUtilities::createGraphicsPipeline(cfg).pipeline;
        };

        pipelineStencilIncNoColor = makeStencilPipeline(stencilIncOp, false);
        pipelineStencilIncColor = makeStencilPipeline(stencilIncOp, true);
        pipelineStencilTest = makeStencilPipeline(stencilTestOp, true);
        pipelineStencilDecNoColor = makeStencilPipeline(stencilDecOp, false);
    }

    void UIRenderPipeline::createFramebuffers()
    {
        framebuffers.resize(offscreenResources.colorImages.size());
        for (uint32_t i = 0; i < framebuffers.size(); i++)
        {
            std::array<vk::ImageView, 2> attachments = {
                offscreenResources.colorImages[i].colorImageView,
                offscreenResources.uiStencilImage.stencilImageView
            };
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
}
