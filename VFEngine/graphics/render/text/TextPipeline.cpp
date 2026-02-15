#include "TextPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "text/TextLayout.hpp"
#include "resource/Types.hpp"
#include <algorithm>
#include <cmath>

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
        createRenderPass();
        createDescriptorSetLayout();
        createDescriptorPool();

        bufferManager.init();
        fontCache.init();

        createDefaultDescriptorSet();
        createPipeline();
        createFramebuffers();

        initialized = true;
    }

    void TextPipeline::loadShader()
    {
        textShader = std::make_shared<core::Shader>(device);
        textShader->readShader("../../resources/shaders/text/text.glsl");
    }

    void TextPipeline::recreate()
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

    void TextPipeline::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

        for (auto& framebuffer : framebuffers)
        {
            dev.destroyFramebuffer(framebuffer);
        }
        framebuffers.clear();

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

        if (renderPass)
            dev.destroyRenderPass(renderPass);

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
            const CachedFont* cached = fontCache.getFont(textEntity.fontPath);
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
            auto layout = ::text::layoutText(
                fontData,
                textEntity.text,
                textEntity.fontSize,
                textEntity.maxWidth,
                textEntity.lineSpacing,
                textEntity.letterSpacing
            );

            // Apply alignment offsets if needed
            bool hasHAlign = textEntity.horizontalAlignment != 0;
            bool hasVAlign = textEntity.verticalAlignment != 0 && textEntity.rectHeight > 0.0f;

            std::vector<float> lineOffsetX;
            float verticalOffset = 0.0f;

            if ((hasHAlign || hasVAlign) && !layout.glyphs.empty())
            {
                // Group glyphs by line
                struct LineInfo { size_t startIdx = 0; size_t count = 0; float minX = 0.0f; float maxX = 0.0f; };
                std::vector<LineInfo> lines;
                float currentLineY = layout.glyphs[0].offset.y;
                LineInfo currentLine{0, 0, layout.glyphs[0].offset.x, layout.glyphs[0].offset.x + layout.glyphs[0].size.x};

                for (size_t gi = 0; gi < layout.glyphs.size(); ++gi)
                {
                    const auto& g = layout.glyphs[gi];
                    if (std::abs(g.offset.y - currentLineY) > 0.1f)
                    {
                        lines.push_back(currentLine);
                        currentLineY = g.offset.y;
                        currentLine = {gi, 0, g.offset.x, g.offset.x + g.size.x};
                    }
                    currentLine.count++;
                    currentLine.minX = std::min(currentLine.minX, g.offset.x);
                    currentLine.maxX = std::max(currentLine.maxX, g.offset.x + g.size.x);
                }
                lines.push_back(currentLine);

                // Horizontal alignment per line — use maxWidth (rect width in layout units) if available
                float contentWidth = textEntity.maxWidth > 0.0f ? textEntity.maxWidth : layout.boundingBox.x;
                lineOffsetX.resize(lines.size(), 0.0f);
                if (hasHAlign)
                {
                    for (size_t li = 0; li < lines.size(); ++li)
                    {
                        float lineWidth = lines[li].maxX - lines[li].minX;
                        if (textEntity.horizontalAlignment == 1)
                            lineOffsetX[li] = (contentWidth - lineWidth) * 0.5f;
                        else if (textEntity.horizontalAlignment == 2)
                            lineOffsetX[li] = contentWidth - lineWidth;
                    }
                }

                // Vertical alignment
                if (hasVAlign)
                {
                    float totalHeight = layout.boundingBox.y;
                    if (textEntity.verticalAlignment == 1)
                        verticalOffset = (textEntity.rectHeight - totalHeight) * 0.5f;
                    else if (textEntity.verticalAlignment == 2)
                        verticalOffset = textEntity.rectHeight - totalHeight;
                }
            }

            auto& instances = fontInstances[textEntity.fontPath];

            if (!lineOffsetX.empty() || verticalOffset != 0.0f)
            {
                size_t lineIdx = 0;
                float prevY = layout.glyphs.empty() ? 0.0f : layout.glyphs[0].offset.y;

                for (size_t gi = 0; gi < layout.glyphs.size(); ++gi)
                {
                    const auto& glyph = layout.glyphs[gi];

                    if (gi > 0 && std::abs(glyph.offset.y - prevY) > 0.1f)
                    {
                        lineIdx++;
                        prevY = glyph.offset.y;
                    }

                    TextCharInstance inst{};
                    inst.worldPosition = textEntity.worldPosition;
                    inst.fontSize = textEntity.fontSize;
                    inst.charOffset = glyph.offset;
                    if (lineIdx < lineOffsetX.size())
                        inst.charOffset.x += lineOffsetX[lineIdx];
                    inst.charOffset.y += verticalOffset;
                    inst.charSize = glyph.size;
                    inst.uvRect = glyph.uvRect;
                    inst.color = textEntity.color;
                    inst.renderMode = textEntity.renderMode;
                    inst.entityId = textEntity.entityId;
                    inst.sdfEdge = sdfEdge;
                    inst.sdfSmooth = sdfSmooth;
                    instances.push_back(inst);
                }
            }
            else
            {
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
                    instances.push_back(inst);
                }
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

        for (const auto& batch : fontBatches)
        {
            auto it = fontDescriptorSets.find(batch.fontPath);
            vk::DescriptorSet descSet = (it != fontDescriptorSets.end())
                ? it->second : defaultDescriptorSet;

            // Determine glyphMode from cached font data
            const CachedFont* cached = fontCache.getFont(batch.fontPath);
            uint32_t glyphMode = (cached && cached->isColorFont) ? 1u : 0u;

            TextPushConstants pushConstants{};
            pushConstants.viewportSize = viewportSize;
            pushConstants.glyphMode = glyphMode;

            commandBuffer.pushConstants(pipelineLayout,
                                         vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                         0, sizeof(TextPushConstants), &pushConstants);

            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                              0, descSet, nullptr);

            commandBuffer.drawIndexed(6, batch.instanceCount, 0, 0, batch.firstInstance);
        }

        commandBuffer.endRenderPass();
    }
}
