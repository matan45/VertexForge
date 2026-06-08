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
            if (label.fontPath.empty() || label.text.empty())
            {
                continue;
            }

            const render::text::CachedFont* cached = fontCache.getFont(label.fontPath);
            if (!cached || !cached->fontData)
            {
                continue;
            }

            const auto& fontData = *cached->fontData;

            // Compute SDF parameters
            float sdfEdge = fontData.sdfParams.edgeValue;
            float sdfSmooth = fontData.isSDF() ? (fontData.sdfParams.spread > 0.0f
                ? 1.0f / fontData.sdfParams.spread * 0.5f
                : 0.1f) : 0.0f;

            // Layout text using shared text layout engine
            float maxWidth = label.wordWrap ? label.size.x : 0.0f;
            auto layout = ::text::layoutText(
                fontData,
                label.text,
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

            // Apply horizontal alignment per line
            struct LineInfo
            {
                size_t startIdx = 0;
                size_t count = 0;
                float minX = 0.0f;
                float maxX = 0.0f;
            };
            std::vector<LineInfo> lines;

            float currentLineY = layout.glyphs[0].offset.y;
            LineInfo currentLine;
            currentLine.startIdx = 0;
            currentLine.count = 0;
            currentLine.minX = layout.glyphs[0].offset.x;
            currentLine.maxX = layout.glyphs[0].offset.x + layout.glyphs[0].size.x;

            for (size_t i = 0; i < layout.glyphs.size(); ++i)
            {
                const auto& glyph = layout.glyphs[i];

                if (std::abs(glyph.offset.y - currentLineY) > 0.1f)
                {
                    lines.push_back(currentLine);
                    currentLineY = glyph.offset.y;
                    currentLine.startIdx = i;
                    currentLine.count = 0;
                    currentLine.minX = glyph.offset.x;
                    currentLine.maxX = glyph.offset.x + glyph.size.x;
                }

                currentLine.count++;
                currentLine.minX = std::min(currentLine.minX, glyph.offset.x);
                currentLine.maxX = std::max(currentLine.maxX, glyph.offset.x + glyph.size.x);
            }
            lines.push_back(currentLine);

            // Compute per-line horizontal offset
            std::vector<float> lineOffsetX(lines.size(), 0.0f);
            for (size_t li = 0; li < lines.size(); ++li)
            {
                float lineWidth = lines[li].maxX - lines[li].minX;
                switch (label.horizontalAlignment)
                {
                case 1: // Center
                    lineOffsetX[li] = (label.size.x - lineWidth) * 0.5f;
                    break;
                case 2: // Right
                    lineOffsetX[li] = label.size.x - lineWidth;
                    break;
                default: // Left (0)
                    lineOffsetX[li] = 0.0f;
                    break;
                }
            }

            // Compute vertical alignment offset
            float totalHeight = layout.boundingBox.y;
            float verticalOffset = 0.0f;
            switch (label.verticalAlignment)
            {
            case 1: // Middle
                verticalOffset = (label.size.y - totalHeight) * 0.5f;
                break;
            case 2: // Bottom
                verticalOffset = label.size.y - totalHeight;
                break;
            default: // Top (0)
                verticalOffset = 0.0f;
                break;
            }

            // Build glyph instances
            ScissorKey scissorKey{
                static_cast<int32_t>(label.scissorRect.x),
                static_cast<int32_t>(label.scissorRect.y),
                static_cast<int32_t>(label.scissorRect.z),
                static_cast<int32_t>(label.scissorRect.w)
            };

            uint32_t styleFlags = 0;
            if (label.fontStyle == components::FontStyle::Bold ||
                label.fontStyle == components::FontStyle::BoldItalic) styleFlags |= 0x1u;
            if (label.fontStyle == components::FontStyle::Italic ||
                label.fontStyle == components::FontStyle::BoldItalic) styleFlags |= 0x2u;

            size_t glyphIdx = 0;
            for (size_t li = 0; li < lines.size(); ++li)
            {
                for (size_t gi = 0; gi < lines[li].count; ++gi, ++glyphIdx)
                {
                    const auto& glyph = layout.glyphs[glyphIdx];

                    glm::vec2 alignedOffset = glyph.offset;
                    alignedOffset.x += lineOffsetX[li];
                    alignedOffset.y += verticalOffset;

                    UITextCharInstance inst{};
                    inst.posAndSize = glm::vec4(
                        label.position.x + alignedOffset.x,
                        label.position.y + alignedOffset.y,
                        glyph.size.x,
                        glyph.size.y
                    );
                    inst.uvRect = glyph.uvRect;
                    inst.color = label.color;
                    inst.sdfParams = glm::vec2(sdfEdge, sdfSmooth);
                    inst.styleFlags = styleFlags;

                    scissorMap[scissorKey].push_back({label.fontPath, inst,
                        label.stencilOp, label.stencilRef});
                }
            }
        }

        // Build ordered instance buffer and scissor groups
        std::vector<UITextCharInstance> allInstances;

        for (auto& [key, entries] : scissorMap)
        {
            UITextScissorGroup group;
            group.scissorRect = glm::vec4(key.x, key.y, key.w, key.h);

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

                // Determine glyphMode from cached font data
                const render::text::CachedFont* cached = fontCache.getFont(batch.fontPath);
                uint32_t glyphMode = (cached && cached->isColorFont) ? 1u : 0u;

                UITextPushConstants pushConstants{};
                pushConstants.viewportSize = viewportSize;
                pushConstants.glyphMode = glyphMode;

                commandBuffer.pushConstants(pipelineLayout,
                                             vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                             0, sizeof(UITextPushConstants), &pushConstants);

                commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                                  0, descSet, nullptr);

                commandBuffer.drawIndexed(6, batch.instanceCount, 0, 0, batch.firstInstance);
                render::FrameDrawStats::count();
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
                                                          uint32_t imageIndex) const
    {
        if (!initialized || totalInstanceCount == 0)
        {
            return;
        }

        bool hasDisplay = !offscreenResources.displayColorImages.empty();
        auto& colorSrc = hasDisplay ? offscreenResources.displayColorImages : offscreenResources.colorImages;

        auto colorAttach = core::colorLoad(colorSrc[imageIndex].colorImageView);
        auto stencilAttach = core::stencilLoad(offscreenResources.uiStencilImage.stencilImageView);

        core::DynamicRenderingInfo info{};
        info.extent = swapChain.getDisplayExtent();
        info.colorAttachments = {colorAttach};
        info.stencilAttachment = stencilAttach;

        core::beginDynamicRendering(commandBuffer, info);

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

                // Determine glyphMode from cached font data
                const render::text::CachedFont* cached = fontCache.getFont(batch.fontPath);
                uint32_t glyphMode = (cached && cached->isColorFont) ? 1u : 0u;

                UITextPushConstants pushConstants{};
                pushConstants.viewportSize = viewportSize;
                pushConstants.glyphMode = glyphMode;

                commandBuffer.pushConstants(pipelineLayout,
                                             vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                             0, sizeof(UITextPushConstants), &pushConstants);

                commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                                  0, descSet, nullptr);

                commandBuffer.drawIndexed(6, batch.instanceCount, 0, 0, batch.firstInstance);
                render::FrameDrawStats::count();
            }
        }

        core::endDynamicRendering(commandBuffer);
    }
}
