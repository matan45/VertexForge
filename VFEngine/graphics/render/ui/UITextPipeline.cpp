#include "UITextPipeline.hpp"
#include "UIRenderTypes.hpp"
#include "../text/TextFontCache.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "text/TextLayout.hpp"
#include "resource/Types.hpp"
#include "print/Logger.hpp"
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
        createRenderPass();
        createDescriptorSetLayout();
        createDescriptorPool();

        bufferManager.init();

        createDefaultDescriptorSet();
        createPipeline();
        createFramebuffers();

        initialized = true;
    }

    void UITextPipeline::loadShader()
    {
        uiTextShader = std::make_shared<core::Shader>(device);
        uiTextShader->readShader("../../resources/shaders/ui/ui_text.glsl");
    }

    void UITextPipeline::recreate()
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

    void UITextPipeline::cleanUp()
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
        scissorGroups.clear();
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

        if (uiTextShader)
        {
            uiTextShader->cleanUp();
            uiTextShader.reset();
        }

        initialized = false;
    }

    void UITextPipeline::createRenderPass()
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

        vk::AttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = nullptr;

        std::array<vk::AttachmentDescription, 1> attachments = {colorAttachment};

        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        renderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);
    }

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
            .renderPass = renderPass,
            .extent = swapChain.getSwapchainExtent(),
            .shaderStages = uiTextShader->getShaderStages(),
            .vertexBindings = {vertexBinding, instanceBinding},
            .vertexAttributes = std::move(allAttribs),
            .topology = vk::PrimitiveTopology::eTriangleList,
            .descriptorSetLayouts = {descriptorSetLayout},
            .pushConstantSize = sizeof(UITextPushConstants),
            .pushConstantStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .cullMode = vk::CullModeFlagBits::eNone,
            .depthTestEnable = false,
            .depthWriteEnable = false,
            .blendEnable = true,
            .dynamicStates = { vk::DynamicState::eScissor }
        };

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        graphicsPipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;
    }

    void UITextPipeline::createFramebuffers()
    {
        framebuffers.resize(offscreenResources.colorImages.size());

        for (uint32_t i = 0; i < framebuffers.size(); i++)
        {
            vk::ImageView colorView = offscreenResources.colorImages[i].colorImageView;
            std::array<vk::ImageView, 1> attachments = {colorView};

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
            loggerWarning("UI text font descriptor limit reached ({})", MAX_FONT_DESCRIPTORS);
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

            // Apply horizontal alignment per line
            // Group glyphs by line (detect y-offset changes)
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
                    // New line detected
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

                    scissorMap[scissorKey].push_back({label.fontPath, inst});
                }
            }
        }

        // Build ordered instance buffer and scissor groups
        std::vector<UITextCharInstance> allInstances;

        for (auto& [key, entries] : scissorMap)
        {
            UITextScissorGroup group;
            group.scissorRect = glm::vec4(key.x, key.y, key.w, key.h);

            // Sub-group by font within this scissor group
            std::unordered_map<std::string_view, std::vector<UITextCharInstance>> fontedInstances;
            for (auto& entry : entries)
            {
                fontedInstances[entry.fontPath].push_back(entry.instance);
            }

            for (auto& [path, instances] : fontedInstances)
            {
                UITextFontBatch batch;
                batch.fontPath = std::string(path);
                batch.firstInstance = static_cast<uint32_t>(allInstances.size());
                batch.instanceCount = static_cast<uint32_t>(instances.size());
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
                scissor.extent = swapChain.getSwapchainExtent();
            }
            commandBuffer.setScissor(0, 1, &scissor);

            for (const auto& batch : group.batches)
            {
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
            }
        }

        commandBuffer.endRenderPass();
    }
}
