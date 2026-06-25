#include "UIRenderPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Texture.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "resource/Types.hpp"
#include "print/Log.hpp"

namespace render::ui
{
    UIRenderPipeline::UIRenderPipeline(core::Device& device, core::SwapChain& swapChain,
                                       core::OffscreenResources& offscreenResources)
        : device{device}, swapChain{swapChain}, offscreenResources{offscreenResources}, bufferManager{device},
          uiBindless{device, UI_BINDLESS_CAPACITY}
    {
    }

    UIRenderPipeline::~UIRenderPipeline() = default;

    void UIRenderPipeline::init()
    {
        loadShader();
        uiBindless.init();
        bufferManager.init();

        {
            resource::TextureData whiteTexData;
            whiteTexData.width = 1; whiteTexData.height = 1; whiteTexData.numbersOfChannels = 4; whiteTexData.mipLevels = 1;
            whiteTexData.mipData.push_back({.width = 1, .height = 1, .dataSize = 4, .data = {255, 255, 255, 255}});

            auto texture = std::make_unique<core::Texture>(device);
            texture->loadTextureFromData(whiteTexData, vk::Format::eR8G8B8A8Unorm, false);

            // White 1x1 becomes the bindless default at index 0 (textureIndex 0 fallback).
            uiBindless.setDefaultTexture(texture->getImageView(), texture->getSampler());

            TextureEntry entry;
            entry.texture = std::move(texture);
            entry.bindlessIndex = 0;
            textureCache.emplace("__white_1x1__", std::move(entry));
        }

        createPipeline();
        initialized = true;
    }

    void UIRenderPipeline::loadShader()
    {
        uiShader = std::make_shared<core::Shader>(device);
        uiShader->readShader("../../resources/shaders/ui/ui_image.glsl");
    }

    void UIRenderPipeline::recreate()
    {
        auto& dev = device.getLogicalDevice();
        if (pipelineNormal) dev.destroyPipeline(pipelineNormal);
        if (pipelineStencilIncNoColor) dev.destroyPipeline(pipelineStencilIncNoColor);
        if (pipelineStencilIncColor) dev.destroyPipeline(pipelineStencilIncColor);
        if (pipelineStencilTest) dev.destroyPipeline(pipelineStencilTest);
        if (pipelineStencilDecNoColor) dev.destroyPipeline(pipelineStencilDecNoColor);
        dev.destroyPipelineLayout(pipelineLayout);

        createPipeline();
    }

    void UIRenderPipeline::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

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

        uiBindless.cleanup();
        bufferManager.cleanUp();
        if (uiShader) { uiShader->cleanUp(); uiShader.reset(); }
        initialized = false;
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
            .extent = swapChain.getDisplayExtent(), .colorAttachmentFormats = {swapChain.getSceneColorFormat()},
            .stencilAttachmentFormat = vk::Format::eS8Uint,
            .shaderStages = uiShader->getShaderStages(),
            .vertexBindings = {vertexBinding, instanceBinding}, .vertexAttributes = allAttribs,
            .topology = vk::PrimitiveTopology::eTriangleList, .descriptorSetLayouts = {uiBindless.getDescriptorSetLayout()},
            .pushConstantSize = sizeof(UIPushConstants),
            .pushConstantStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .cullMode = vk::CullModeFlagBits::eNone, .depthTestEnable = false, .depthWriteEnable = false,
            // VK-1435: viewport is dynamic so the same pipeline records into the main swapchain
            // target AND the UI Layer Builder's offscreen target at its reference resolution. The
            // main path sets the display extent at record time, so it is behavior-preserving.
            .blendEnable = true, .dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor }
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

        std::vector<vk::DynamicState> stencilDynStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eStencilReference};

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
}
