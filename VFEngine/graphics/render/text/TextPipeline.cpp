#include "TextPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "../../core/ImageUtilities.hpp"
#include "text/TextLayout.hpp"
#include "text/TextEffects.hpp"
#include "text/FontStyleFace.hpp"
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
            // VK-1636: and to a real Bold / Italic sibling face when the family ships
            // one, in which case the shader stops synthesizing that axis. The world
            // text pipeline has no rich text, so one face per entity is enough.
            const uint32_t requestedStyleBits =
                ::text::styleBitsFromFontStyle(static_cast<uint8_t>(textEntity.fontStyle));
            const TextFontCache::StyledFontResolution styled =
                fontCache.resolveStyledFont(textEntity.fontPath, requestedStyleBits);

            const std::string& fontKey = *styled.key;
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

            // VK-1637: the box rules - which box, which wrap width, which gate - are pure,
            // so they live in utilities/text where the CPU-only test suite can reach them;
            // this TU is Vulkan-bound. clipSupported is false because this pipeline issues
            // no vk::CommandBuffer::setScissor, so TextOverflow::Clip degrades to Overflow
            // while still round-tripping losslessly through the scene file.
            ::text::TextBoxRequest boxRequest;
            boxRequest.maxWidth = textEntity.maxWidth;
            boxRequest.rectHeight = textEntity.rectHeight;
            boxRequest.wordWrap = textEntity.wordWrap;
            boxRequest.overflow = static_cast<uint8_t>(textEntity.overflow);
            boxRequest.horizontal = textEntity.horizontalAlignment;
            boxRequest.vertical = textEntity.verticalAlignment;
            boxRequest.clipSupported = false;
            boxRequest.requireHeightForVAlign = true;
            const ::text::TextBoxPolicy box = ::text::resolveTextBox(boxRequest);

            // Layout text using shared text layout engine
            auto layout = ::text::layoutText(
                fontData,
                textEntity.text,
                textEntity.fontSize,
                box.wrapWidth,
                textEntity.lineSpacing,
                textEntity.letterSpacing
            );

            // VK-1637: per-line ellipsis, BEFORE alignment - the ordering UITextPipeline
            // uses, pinned by tests/test_textlayout_alignment.cpp - so a truncated line
            // centres on its truncated width rather than its original one. Being per-line,
            // it covers both wrap states: every wrapped line, or the single un-wrapped one.
            if (box.ellipsis)
            {
                // One face per entity here (this pipeline has no rich text), so the
                // baselineShift UITextPipeline passes is identically 0 - omit it.
                ::text::applyEllipsis(layout, fontData, textEntity.fontSize,
                                      box.ellipsisWidth, textEntity.letterSpacing);
                if (layout.glyphs.empty())
                {
                    // Before fontInstances[fontKey] below, so a fully truncated entity
                    // never creates an empty batch bucket.
                    continue;
                }
            }

            // VK-1632: alignment lives in utilities/text now, shared with UITextPipeline.
            const auto lineMetrics = ::text::computeLineMetrics(
                fontData, textEntity.fontSize, textEntity.lineSpacing);

            ::text::AlignParams alignParams;
            alignParams.horizontal = box.horizontal;
            alignParams.vertical = box.vertical;
            // boundingBox.x is only known after layout, which is why resolveTextBox hands
            // back the flag rather than the width.
            alignParams.contentSize = glm::vec2(
                box.alignToInkWidth ? layout.boundingBox.x : box.alignWidth,
                box.alignHeight);
            alignParams.lineHeight = lineMetrics.lineHeight;
            alignParams.singleLineHeight = lineMetrics.singleLineHeight;

            ::text::applyAlignment(layout, alignParams);

            auto& instances = fontInstances[fontKey];

            // VK-1636: only the axes the resolved face does NOT provide are still
            // faked. A family with a real Bold gets 0 here and the shader's threshold
            // bias never runs; one without gets exactly what it got before.
            const uint32_t styleFlags = styled.synthesizedBits;

            // VK-1635: authored distances are layout pixels; the instance wants atlas
            // texels, and layout pixels per texel is exactly the `scale` TextLayout uses.
            // Because they are relative to fontSize rather than to the screen, a world-space
            // outline keeps its proportion to the glyph as the text recedes.
            const float effectScale =
                static_cast<float>(fontData.metadata.baseFontSize) > 0.0f
                    ? textEntity.fontSize / static_cast<float>(fontData.metadata.baseFontSize)
                    : 0.0f;
            const ::text::TextEffectInstance effect = ::text::buildTextEffectInstance(
                textEntity.effects, effectScale, sdfSmooth > 0.0f);

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
                inst.effectParams = effect.params;
                inst.effectColors = effect.colors;
                inst.effectMargin = effect.marginPx;
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
