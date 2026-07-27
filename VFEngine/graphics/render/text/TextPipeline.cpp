#include "TextPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "../../core/ImageUtilities.hpp"
#include "text/TextLayout.hpp"
#include "resource/Types.hpp"
#include <algorithm>

namespace render::text
{
    TextPipeline::TextPipeline(core::Device& device, core::SwapChain& swapChain,
                               core::OffscreenResources& offscreenResources)
        : device{device}
        , swapChain{swapChain}
        , offscreenResources{offscreenResources}
        , bufferManager{device}
        , fontCache{device}
    {
    }

    TextPipeline::~TextPipeline() = default;

    void TextPipeline::init()
    {
        loadShader();
        createDescriptorSetLayout();
        createDescriptorPool();

        bufferManager.init();
        fontCache.init();

        createDefaultDescriptorSet();
        createPipeline();

        initialized = true;
    }

    void TextPipeline::loadShader()
    {
        textShader = std::make_shared<core::Shader>(device);
        textShader->readShader("../../resources/shaders/text/text.glsl");
    }

    void TextPipeline::recreate()
    {
        device.getLogicalDevice().destroyPipeline(graphicsPipeline);
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);

        createPipeline();
    }

    void TextPipeline::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

        if (graphicsPipeline) dev.destroyPipeline(graphicsPipeline);
        if (pipelineLayout) dev.destroyPipelineLayout(pipelineLayout);

        fontDescriptorSets.clear();
        fontBatches.clear();
        totalInstanceCount = 0;

        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (descriptorSetLayout) dev.destroyDescriptorSetLayout(descriptorSetLayout);

        bufferManager.cleanUp();
        fontCache.cleanUp();

        if (textShader)
        {
            textShader->cleanUp();
            textShader.reset();
        }

        initialized = false;
    }

    void TextPipeline::updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                        const glm::vec3& cameraPos) const
    {
        bufferManager.updateCameraUBO(view, projection, cameraPos);
    }

    void TextPipeline::setTextDrawList(const std::vector<TextRenderData>& textEntities)
    {
        fontBatches.clear();
        totalInstanceCount = 0;

        if (textEntities.empty())
        {
            bufferManager.updateInstanceBuffer({});
            return;
        }

        // Process pending font loads
        fontCache.processPendingLoads();

        // Request any fonts that aren't loaded yet
        for (const auto& textEntity : textEntities)
        {
            if (!textEntity.fontPath.empty())
            {
                fontCache.requestFont(textEntity.fontPath);
            }
        }

        // Group text entities by font, lay out text, and build instances
        std::unordered_map<std::string, std::vector<TextCharInstance>> fontInstances;

        for (const auto& textEntity : textEntities)
        {
            // VK-1628: falls back to the default font when this one is missing or
            // still loading. Everything below keys off fontKey, never fontPath.
            const std::string& fontKey = fontCache.resolveFontKey(textEntity.fontPath);
            const CachedFont* cached = fontCache.getFont(fontKey);
            if (!cached || !cached->fontData)
            {
                continue;
            }

            const auto& fontData = *cached->fontData;

            // Compute SDF parameters. VK-1631: sdfSmooth is now only the SDF / non-SDF
            // signal - the shader derives the on-screen AA band from fwidth().
            float sdfEdge = fontData.sdfParams.edgeValue;
            float sdfSmooth = resource::sdfSmoothWidth(fontData);

            // Layout text using shared text layout engine
            auto layout = ::text::layoutText(
                fontData,
                textEntity.text,
                textEntity.fontSize,
                textEntity.maxWidth,
                textEntity.lineSpacing,
                textEntity.letterSpacing
            );

            // VK-1632: alignment lives in utilities/text now, shared with UITextPipeline.
            // The gating policy stays here: this pipeline falls back to the laid-out width
            // when there is no explicit rect, and ignores vertical alignment without one.
            const auto lineMetrics = ::text::computeLineMetrics(
                fontData, textEntity.fontSize, textEntity.lineSpacing);

            ::text::AlignParams alignParams;
            alignParams.horizontal = ::text::toHAlign(textEntity.horizontalAlignment);
            alignParams.vertical = textEntity.rectHeight > 0.0f
                                       ? ::text::toVAlign(textEntity.verticalAlignment)
                                       : ::text::VAlign::Top;
            alignParams.contentSize = glm::vec2(
                textEntity.maxWidth > 0.0f ? textEntity.maxWidth : layout.boundingBox.x,
                textEntity.rectHeight);
            alignParams.lineHeight = lineMetrics.lineHeight;
            alignParams.singleLineHeight = lineMetrics.singleLineHeight;

            ::text::applyAlignment(layout, alignParams);

            auto& instances = fontInstances[fontKey];

            uint32_t styleFlags = 0;
            if (textEntity.fontStyle == components::FontStyle::Bold ||
                textEntity.fontStyle == components::FontStyle::BoldItalic) styleFlags |= 0x1u;
            if (textEntity.fontStyle == components::FontStyle::Italic ||
                textEntity.fontStyle == components::FontStyle::BoldItalic) styleFlags |= 0x2u;

            for (const auto& glyph : layout.glyphs)
            {
                TextCharInstance inst{};
                inst.worldPosition = textEntity.worldPosition;
                inst.fontSize = textEntity.fontSize;
                inst.charOffset = glyph.offset;
                inst.charSize = glyph.size;
                inst.uvRect = glyph.uvRect;
                inst.color = textEntity.color;
                inst.renderMode = textEntity.renderMode;
                inst.entityId = textEntity.entityId;
                inst.sdfEdge = sdfEdge;
                inst.sdfSmooth = sdfSmooth;
                inst.styleFlags = styleFlags;
                instances.push_back(inst);
            }
        }

        // Build ordered instance buffer and batch list
        std::vector<TextCharInstance> allInstances;
        for (auto& [fontPath, instances] : fontInstances)
        {
            if (instances.empty())
            {
                continue;
            }

            FontBatch batch;
            batch.fontPath = fontPath;
            batch.firstInstance = static_cast<uint32_t>(allInstances.size());
            batch.instanceCount = static_cast<uint32_t>(instances.size());
            fontBatches.push_back(std::move(batch));

            allInstances.insert(allInstances.end(), instances.begin(), instances.end());
        }

        totalInstanceCount = static_cast<uint32_t>(allInstances.size());
        bufferManager.updateInstanceBuffer(allInstances);

        // Create/update descriptor sets for each font batch
        for (const auto& batch : fontBatches)
        {
            getOrCreateFontDescriptorSet(batch.fontPath);
        }
    }

    void TextPipeline::recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                            uint32_t imageIndex) const
    {
        if (!initialized || totalInstanceCount == 0)
        {
            return;
        }

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.colorImages[imageIndex].colorImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        auto colorAttach = core::colorLoad(offscreenResources.colorImages[imageIndex].colorImageView);
        auto depthAttach = core::depthLoad(offscreenResources.depthImage.depthImageView);

        core::DynamicRenderingInfo info{};
        info.extent = swapChain.getSwapchainExtent();
        info.colorAttachments = {colorAttach};
        info.depthAttachment = depthAttach;

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

        for (const auto& batch : fontBatches)
        {
            auto it = fontDescriptorSets.find(batch.fontPath);
            vk::DescriptorSet descSet = (it != fontDescriptorSets.end())
                ? it->second : defaultDescriptorSet;

            // The cache validates the atlas format and stores its shader-facing mode.
            const CachedFont* cached = fontCache.getFont(batch.fontPath);
            uint32_t glyphMode = cached ? cached->glyphMode : 0u;

            TextPushConstants pushConstants{};
            pushConstants.viewportSize = viewportSize;
            pushConstants.glyphMode = glyphMode;
            pushConstants.pxRange = cached ? cached->pxRange : 0.0f;

            commandBuffer.pushConstants(pipelineLayout,
                                         vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                         0, sizeof(TextPushConstants), &pushConstants);

            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                              0, descSet, nullptr);

            commandBuffer.drawIndexed(6, batch.instanceCount, 0, 0, batch.firstInstance);
            render::FrameDrawStats::count(render::DrawCategory::UIText);
        }

        core::endDynamicRendering(commandBuffer);

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.colorImages[imageIndex].colorImage,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);
    }

    void TextPipeline::recordCommandBufferGraphManaged(const vk::CommandBuffer& commandBuffer,
                                            uint32_t imageIndex) const
    {
        if (!initialized || totalInstanceCount == 0)
        {
            return;
        }

        auto colorAttach = core::colorLoad(offscreenResources.colorImages[imageIndex].colorImageView);
        auto depthAttach = core::depthLoad(offscreenResources.depthImage.depthImageView);

        core::DynamicRenderingInfo info{};
        info.extent = swapChain.getSwapchainExtent();
        info.colorAttachments = {colorAttach};
        info.depthAttachment = depthAttach;

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

        for (const auto& batch : fontBatches)
        {
            auto it = fontDescriptorSets.find(batch.fontPath);
            vk::DescriptorSet descSet = (it != fontDescriptorSets.end())
                ? it->second : defaultDescriptorSet;

            // The cache validates the atlas format and stores its shader-facing mode.
            const CachedFont* cached = fontCache.getFont(batch.fontPath);
            uint32_t glyphMode = cached ? cached->glyphMode : 0u;

            TextPushConstants pushConstants{};
            pushConstants.viewportSize = viewportSize;
            pushConstants.glyphMode = glyphMode;
            pushConstants.pxRange = cached ? cached->pxRange : 0.0f;

            commandBuffer.pushConstants(pipelineLayout,
                                         vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                         0, sizeof(TextPushConstants), &pushConstants);

            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                              0, descSet, nullptr);

            commandBuffer.drawIndexed(6, batch.instanceCount, 0, 0, batch.firstInstance);
            render::FrameDrawStats::count(render::DrawCategory::UIText);
        }

        core::endDynamicRendering(commandBuffer);
    }
}
