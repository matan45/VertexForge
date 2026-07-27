#include "UITextPipeline.hpp"
#include "UIRenderTypes.hpp"
#include "../text/TextFontCache.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "../../core/ImageUtilities.hpp"
#include "text/TextLayout.hpp"
#include "text/RichTextParser.hpp"
#include "resource/Types.hpp"
#include <algorithm>
#include <string_view>
#include <cmath>

namespace render::ui
{
    UITextPipeline::UITextPipeline(core::Device& device, core::SwapChain& swapChain,
                                    core::OffscreenResources& offscreenResources,
                                    render::text::TextFontCache& fontCache)
        : device{device}
        , swapChain{swapChain}
        , offscreenResources{offscreenResources}
        , fontCache{fontCache}
        , bufferManager{device}
    {
    }

    UITextPipeline::~UITextPipeline() = default;

    void UITextPipeline::init()
    {
        loadShader();
        createDescriptorSetLayout();
        createDescriptorPool();

        bufferManager.init();

        createDefaultDescriptorSet();
        createPipeline();

        initialized = true;
    }

    void UITextPipeline::loadShader()
    {
        uiTextShader = std::make_shared<core::Shader>(device);
        uiTextShader->readShader("../../resources/shaders/ui/ui_text.glsl");
    }

    void UITextPipeline::recreate()
    {
        device.getLogicalDevice().destroyPipeline(graphicsPipeline);
        if (pipelineStencilTest) device.getLogicalDevice().destroyPipeline(pipelineStencilTest);
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);

        createPipeline();
    }

    void UITextPipeline::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

        if (graphicsPipeline) dev.destroyPipeline(graphicsPipeline);
        if (pipelineStencilTest) dev.destroyPipeline(pipelineStencilTest);
        if (pipelineLayout) dev.destroyPipelineLayout(pipelineLayout);

        fontDescriptorSets.clear();
        scissorGroups.clear();
        totalInstanceCount = 0;

        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (descriptorSetLayout) dev.destroyDescriptorSetLayout(descriptorSetLayout);

        bufferManager.cleanUp();

        if (uiTextShader)
        {
            uiTextShader->cleanUp();
            uiTextShader.reset();
        }

        initialized = false;
    }

    void UITextPipeline::setUITextDrawList(const std::vector<UITextRenderData>& labels)
    {
        scissorGroups.clear();
        totalInstanceCount = 0;

        if (labels.empty())
        {
            bufferManager.updateInstanceBuffer({});
            return;
        }

        // Process pending font loads
        fontCache.processPendingLoads();

        // Request any fonts that aren't loaded yet
        for (const auto& label : labels)
        {
            if (!label.fontPath.empty())
            {
                fontCache.requestFont(label.fontPath);
            }
        }

        // Group by scissor rect, then by font within each group
        struct ScissorKey
        {
            int32_t x, y, w, h;
            bool overlay;
            bool operator==(const ScissorKey& o) const
            {
                return x == o.x && y == o.y && w == o.w && h == o.h && overlay == o.overlay;
            }
        };
        struct ScissorKeyHash
        {
            size_t operator()(const ScissorKey& k) const
            {
                size_t h = std::hash<int32_t>{}(k.x);
                h ^= std::hash<int32_t>{}(k.y) << 1;
                h ^= std::hash<int32_t>{}(k.w) << 2;
                h ^= std::hash<int32_t>{}(k.h) << 3;
                h ^= std::hash<bool>{}(k.overlay) << 4;
                return h;
            }
        };

        struct GlyphEntry
        {
            std::string_view fontPath;
            UITextCharInstance instance;
            UIStencilOp stencilOp = UIStencilOp::None;
            uint8_t stencilRef = 0;
        };

        std::unordered_map<ScissorKey, std::vector<GlyphEntry>, ScissorKeyHash> scissorMap;

        for (const auto& label : labels)
        {
            // VK-1628: an empty fontPath is no longer a drop — resolveFontKey below
            // maps it to the default font.
            if (label.text.empty())
            {
                continue;
            }

            // VK-1628: falls back to the default font when this one is missing or
            // still loading. Everything below keys off fontKey, never fontPath.
            const std::string& fontKey = fontCache.resolveFontKey(label.fontPath);
            const render::text::CachedFont* cached = fontCache.getFont(fontKey);
            if (!cached || !cached->fontData)
            {
                continue;
            }

            const auto& fontData = *cached->fontData;

            // Compute SDF parameters. VK-1631: sdfSmooth is now only the SDF / non-SDF
            // signal - the shader derives the on-screen AA band from fwidth().
            float sdfEdge = fontData.sdfParams.edgeValue;
            float sdfSmooth = resource::sdfSmoothWidth(fontData);

            // Rich text: strip markup first, then lay out the stripped text.
            // Per-glyph styles resolve through LayoutGlyph::charIndex below.
            ::text::RichTextResult richText;
            if (label.richText)
            {
                richText = ::text::parseRichText(label.text);
                if (richText.strippedText.empty())
                {
                    continue;
                }
            }
            const std::string& layoutSource = label.richText ? richText.strippedText : label.text;

            // Layout text using shared text layout engine
            float maxWidth = label.wordWrap ? label.size.x : 0.0f;
            auto layout = ::text::layoutText(
                fontData,
                layoutSource,
                label.fontSize,
                maxWidth,
                label.lineSpacing,
                label.letterSpacing
            );

            if (layout.glyphs.empty())
            {
                continue;
            }

            // Ellipsis truncation: per-line. Runs after word-wrap so both
            // wordWrap=true (truncate each wrapped line) and wordWrap=false
            // (truncate the single line) are covered.
            if (label.overflow == components::TextOverflow::Ellipsis &&
                label.size.x > 0.0f)
            {
                ::text::applyEllipsis(layout, fontData, label.fontSize,
                                      label.size.x, label.letterSpacing);
                if (layout.glyphs.empty())
                {
                    continue;
                }
            }

            // VK-1632: alignment lives in utilities/text now, shared with TextPipeline.
            // Lines are grouped by LayoutGlyph::lineY, so a line no longer splits per
            // bearingY; the block height no longer counts a full spaced lineHeight for
            // the trailing line.
            const auto lineMetrics = ::text::computeLineMetrics(
                fontData, label.fontSize, label.lineSpacing);

            ::text::AlignParams alignParams;
            alignParams.horizontal = ::text::toHAlign(label.horizontalAlignment);
            alignParams.vertical = ::text::toVAlign(label.verticalAlignment);
            alignParams.contentSize = label.size;
            alignParams.lineHeight = lineMetrics.lineHeight;
            alignParams.singleLineHeight = lineMetrics.singleLineHeight;

            ::text::applyAlignment(layout, alignParams);

            // Build glyph instances
            ScissorKey scissorKey{
                static_cast<int32_t>(label.scissorRect.x),
                static_cast<int32_t>(label.scissorRect.y),
                static_cast<int32_t>(label.scissorRect.z),
                static_cast<int32_t>(label.scissorRect.w),
                label.overlay
            };

            uint32_t styleFlags = 0;
            if (label.fontStyle == components::FontStyle::Bold ||
                label.fontStyle == components::FontStyle::BoldItalic) styleFlags |= 0x1u;
            if (label.fontStyle == components::FontStyle::Italic ||
                label.fontStyle == components::FontStyle::BoldItalic) styleFlags |= 0x2u;

            for (const auto& glyph : layout.glyphs)
            {
                UITextCharInstance inst{};
                inst.posAndSize = glm::vec4(
                    label.position.x + glyph.offset.x,
                    label.position.y + glyph.offset.y,
                    glyph.size.x,
                    glyph.size.y
                );
                inst.uvRect = glyph.uvRect;
                inst.color = label.color;
                inst.sdfParams = glm::vec2(sdfEdge, sdfSmooth);
                inst.styleFlags = styleFlags;

                // Rich text span overrides. Synthesized glyphs (ellipsis,
                // charIndex == UINT32_MAX) fail the bound check and keep
                // the base label style. Span colors inherit label alpha
                // so fade animations still apply.
                if (label.richText && glyph.charIndex < richText.perCodepoint.size())
                {
                    const auto& span = richText.perCodepoint[glyph.charIndex];
                    inst.styleFlags |= span.styleFlags;
                    if (span.hasColor)
                    {
                        inst.color = glm::vec4(span.color.r, span.color.g,
                                               span.color.b, span.color.a * label.color.a);
                    }
                }

                scissorMap[scissorKey].push_back({fontKey, inst,
                    label.stencilOp, label.stencilRef});
            }
        }

        // Build ordered instance buffer and scissor groups
        std::vector<UITextCharInstance> allInstances;

        for (auto& [key, entries] : scissorMap)
        {
            UITextScissorGroup group;
            group.scissorRect = glm::vec4(key.x, key.y, key.w, key.h);
            group.overlay = key.overlay;

            // Sub-group by font and stencil state within this scissor group
            struct BatchKey {
                std::string_view fontPath;
                UIStencilOp stencilOp;
                uint8_t stencilRef;
                bool operator==(const BatchKey& o) const {
                    return fontPath == o.fontPath && stencilOp == o.stencilOp && stencilRef == o.stencilRef;
                }
            };
            struct BatchKeyHash {
                size_t operator()(const BatchKey& k) const {
                    size_t h = std::hash<std::string_view>{}(k.fontPath);
                    h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(k.stencilOp)) << 1;
                    h ^= std::hash<uint8_t>{}(k.stencilRef) << 2;
                    return h;
                }
            };
            std::unordered_map<BatchKey, std::vector<UITextCharInstance>, BatchKeyHash> fontedInstances;
            for (auto& entry : entries)
            {
                fontedInstances[{entry.fontPath, entry.stencilOp, entry.stencilRef}].push_back(entry.instance);
            }

            for (auto& [batchKey, instances] : fontedInstances)
            {
                UITextFontBatch batch;
                batch.fontPath = std::string(batchKey.fontPath);
                batch.firstInstance = static_cast<uint32_t>(allInstances.size());
                batch.instanceCount = static_cast<uint32_t>(instances.size());
                batch.stencilOp = batchKey.stencilOp;
                batch.stencilRef = batchKey.stencilRef;
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

        // Create/update descriptor sets for each font batch
        for (const auto& group : scissorGroups)
        {
            for (const auto& batch : group.batches)
            {
                getOrCreateFontDescriptorSet(batch.fontPath);
            }
        }
    }

    void UITextPipeline::recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                              uint32_t imageIndex) const
    {
        if (!initialized || totalInstanceCount == 0)
        {
            return;
        }

        bool hasDisplay = !offscreenResources.displayColorImages.empty();
        auto& colorSrc = hasDisplay ? offscreenResources.displayColorImages : offscreenResources.colorImages;

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            colorSrc[imageIndex].colorImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        auto colorAttach = core::colorLoad(colorSrc[imageIndex].colorImageView);
        auto stencilAttach = core::stencilLoad(offscreenResources.uiStencilImage.stencilImageView);

        core::DynamicRenderingInfo info{};
        info.extent = swapChain.getDisplayExtent();
        info.colorAttachments = {colorAttach};
        info.stencilAttachment = stencilAttach;

        core::beginDynamicRendering(commandBuffer, info);

        // The UI pipelines now use a dynamic viewport (VK-1435); set it here too so this
        // (display-extent) path stays valid. Behavior-preserving: same extent as before.
        vk::Viewport viewport{0.0f, 0.0f,
                              static_cast<float>(swapChain.getDisplayExtent().width),
                              static_cast<float>(swapChain.getDisplayExtent().height),
                              0.0f, 1.0f};
        commandBuffer.setViewport(0, 1, &viewport);

        vk::Pipeline currentPipeline = nullptr;

        vk::Buffer vertexBuffers[] = {bufferManager.getQuadVertexBuffer(), bufferManager.getInstanceBuffer()};
        vk::DeviceSize offsets[] = {0, 0};
        commandBuffer.bindVertexBuffers(0, 2, vertexBuffers, offsets);
        commandBuffer.bindIndexBuffer(bufferManager.getQuadIndexBuffer(), 0, vk::IndexType::eUint16);

        glm::vec2 viewportSize(
            static_cast<float>(swapChain.getDisplayExtent().width),
            static_cast<float>(swapChain.getDisplayExtent().height)
        );

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
                scissor.extent = swapChain.getDisplayExtent();
            }
            commandBuffer.setScissor(0, 1, &scissor);

            for (const auto& batch : group.batches)
            {
                // Select pipeline: stencil test if stencilOp == Test, otherwise normal
                vk::Pipeline targetPipeline = (batch.stencilOp == UIStencilOp::Test)
                    ? pipelineStencilTest : graphicsPipeline;

                if (targetPipeline != currentPipeline)
                {
                    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, targetPipeline);
                    currentPipeline = targetPipeline;
                }

                if (batch.stencilOp == UIStencilOp::Test)
                {
                    commandBuffer.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, batch.stencilRef);
                }

                auto it = fontDescriptorSets.find(batch.fontPath);
                vk::DescriptorSet descSet = (it != fontDescriptorSets.end())
                    ? it->second : defaultDescriptorSet;

                // The cache validates the atlas format and stores its shader-facing mode.
                const render::text::CachedFont* cached = fontCache.getFont(batch.fontPath);
                uint32_t glyphMode = cached ? cached->glyphMode : 0u;

                UITextPushConstants pushConstants{};
                pushConstants.viewportSize = viewportSize;
                pushConstants.glyphMode = glyphMode;

                commandBuffer.pushConstants(pipelineLayout,
                                             vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                             0, sizeof(UITextPushConstants), &pushConstants);

                commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                                  0, descSet, nullptr);

                commandBuffer.drawIndexed(6, batch.instanceCount, 0, 0, batch.firstInstance);
                render::FrameDrawStats::count(render::DrawCategory::UIText);
            }
        }

        core::endDynamicRendering(commandBuffer);

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            colorSrc[imageIndex].colorImage,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);
    }

    void UITextPipeline::recordCommandBufferGraphManaged(const vk::CommandBuffer& commandBuffer,
                                                          uint32_t imageIndex, bool overlayPass) const
    {
        // Main swapchain UI text path: record at the display extent (byte-identical to the previous
        // hardcoded behavior, now that the viewport is a dynamic state set to the same extent).
        recordCommandBufferGraphManaged(commandBuffer, imageIndex, swapChain.getDisplayExtent(), overlayPass);
    }

    void UITextPipeline::recordCommandBufferGraphManaged(const vk::CommandBuffer& commandBuffer,
                                                          uint32_t imageIndex, vk::Extent2D targetExtent,
                                                          bool overlayPass) const
    {
        if (!initialized || totalInstanceCount == 0)
        {
            return;
        }

        bool anyGroupInPass = false;
        for (const auto& group : scissorGroups)
        {
            if (group.overlay == overlayPass) { anyGroupInPass = true; break; }
        }
        if (!anyGroupInPass) return;

        bool hasDisplay = !offscreenResources.displayColorImages.empty();
        auto& colorSrc = hasDisplay ? offscreenResources.displayColorImages : offscreenResources.colorImages;

        auto colorAttach = core::colorLoad(colorSrc[imageIndex].colorImageView);
        auto stencilAttach = core::stencilLoad(offscreenResources.uiStencilImage.stencilImageView);

        core::DynamicRenderingInfo info{};
        info.extent = targetExtent;
        info.colorAttachments = {colorAttach};
        info.stencilAttachment = stencilAttach;

        core::beginDynamicRendering(commandBuffer, info);

        // Dynamic viewport matches the render extent. The main path passes the display extent, so
        // this reproduces the previously-baked static viewport exactly.
        vk::Viewport viewport{0.0f, 0.0f,
                              static_cast<float>(targetExtent.width), static_cast<float>(targetExtent.height),
                              0.0f, 1.0f};
        commandBuffer.setViewport(0, 1, &viewport);

        vk::Pipeline currentPipeline = nullptr;

        vk::Buffer vertexBuffers[] = {bufferManager.getQuadVertexBuffer(), bufferManager.getInstanceBuffer()};
        vk::DeviceSize offsets[] = {0, 0};
        commandBuffer.bindVertexBuffers(0, 2, vertexBuffers, offsets);
        commandBuffer.bindIndexBuffer(bufferManager.getQuadIndexBuffer(), 0, vk::IndexType::eUint16);

        glm::vec2 viewportSize(
            static_cast<float>(targetExtent.width),
            static_cast<float>(targetExtent.height)
        );

        for (const auto& group : scissorGroups)
        {
            if (group.overlay != overlayPass) continue;

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
                scissor.extent = targetExtent;
            }
            commandBuffer.setScissor(0, 1, &scissor);

            for (const auto& batch : group.batches)
            {
                // Select pipeline: stencil test if stencilOp == Test, otherwise normal
                vk::Pipeline targetPipeline = (batch.stencilOp == UIStencilOp::Test)
                    ? pipelineStencilTest : graphicsPipeline;

                if (targetPipeline != currentPipeline)
                {
                    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, targetPipeline);
                    currentPipeline = targetPipeline;
                }

                if (batch.stencilOp == UIStencilOp::Test)
                {
                    commandBuffer.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, batch.stencilRef);
                }

                auto it = fontDescriptorSets.find(batch.fontPath);
                vk::DescriptorSet descSet = (it != fontDescriptorSets.end())
                    ? it->second : defaultDescriptorSet;

                // The cache validates the atlas format and stores its shader-facing mode.
                const render::text::CachedFont* cached = fontCache.getFont(batch.fontPath);
                uint32_t glyphMode = cached ? cached->glyphMode : 0u;

                UITextPushConstants pushConstants{};
                pushConstants.viewportSize = viewportSize;
                pushConstants.glyphMode = glyphMode;

                commandBuffer.pushConstants(pipelineLayout,
                                             vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                             0, sizeof(UITextPushConstants), &pushConstants);

                commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                                  0, descSet, nullptr);

                commandBuffer.drawIndexed(6, batch.instanceCount, 0, 0, batch.firstInstance);
                render::FrameDrawStats::count(render::DrawCategory::UIText);
            }
        }

        core::endDynamicRendering(commandBuffer);
    }
}
