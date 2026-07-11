#include "SelectionMaskPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "print/Log.hpp"
#include <cstring>

namespace render::gpudriven
{
    SelectionMaskPipeline::SelectionMaskPipeline(core::Device& device, core::SwapChain& swapChain)
        : device(device), swapChain(swapChain)
    {
    }

    SelectionMaskPipeline::~SelectionMaskPipeline()
    {
        cleanup();
    }

    void SelectionMaskPipeline::init(const SelectionMaskInitInfo& info)
    {
        createSelectionBitsResources(info.maxObjectCount);
        createPipeline(info);
        initialized = pipeline != nullptr;
        if (initialized)
        {
            vfLogInfo("Selection mask pipeline initialized ({} bitmask words)", bitsWordCount);
        }
    }

    void SelectionMaskPipeline::createSelectionBitsResources(uint32_t maxObjectCount)
    {
        auto& dev = device.getLogicalDevice();

        bitsWordCount = (maxObjectCount + 31u) / 32u;
        if (bitsWordCount == 0) bitsWordCount = 1;

        // Set 6: binding 0 = selection bitmask (task stage), binding 1 =
        // sampled scene depth for the fragment visibility test.
        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eTaskEXT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        selectionBitsLayout = dev.createDescriptorSetLayout(layoutInfo);

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[0].descriptorCount = core::MAX_FRAMES_IN_FLIGHT;
        poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = core::MAX_FRAMES_IN_FLIGHT;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = core::MAX_FRAMES_IN_FLIGHT;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        descriptorPool = dev.createDescriptorPool(poolInfo);

        for (auto& frame : bitsFrames)
        {
            core::BufferInfoRequest bufReq(dev, device.getPhysicalDevice());
            bufReq.size = static_cast<vk::DeviceSize>(bitsWordCount) * sizeof(uint32_t);
            bufReq.usage = vk::BufferUsageFlagBits::eStorageBuffer;
            bufReq.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(bufReq, frame.buffer, frame.allocation,
                                                device.getMemoryManager());
            frame.mapped = frame.allocation.mappedPtr;
            std::memset(frame.mapped, 0, bitsWordCount * sizeof(uint32_t));

            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &selectionBitsLayout;
            frame.descriptorSet = dev.allocateDescriptorSets(allocInfo)[0];

            vk::DescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = frame.buffer;
            bufferInfo.offset = 0;
            bufferInfo.range = bufReq.size;

            vk::WriteDescriptorSet write{};
            write.dstSet = frame.descriptorSet;
            write.dstBinding = 0;
            write.descriptorType = vk::DescriptorType::eStorageBuffer;
            write.descriptorCount = 1;
            write.pBufferInfo = &bufferInfo;
            dev.updateDescriptorSets(write, nullptr);
        }
    }

    void SelectionMaskPipeline::createPipeline(const SelectionMaskInitInfo& info)
    {
        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/selection/task_selection_mask.glsl");
        shader->readShader("../../resources/shaders/selection/mesh_selection_mask.glsl");
        shader->readShader("../../resources/shaders/selection/frag_selection_mask.glsl");

        if (shader->getShaderStages().size() < 3)
        {
            vfLogError("SelectionMaskPipeline: failed to load shaders: {}",
                       shader->getLastCompilationError());
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT |
                               vk::ShaderStageFlagBits::eFragment;
        pushRange.offset = 0;
        pushRange.size = sizeof(SelectionMaskPushConstants);

        // Sets 0-5 mirror the depth prepass layout (so the live scene descriptor
        // sets bind unchanged); set 6 adds the selection bitmask.
        std::vector<vk::DescriptorSetLayout> layouts = {
            info.cameraLayout, info.perDrawLayout, info.bindlessTextureLayout,
            info.meshletDataLayout, info.vertexDataLayout, info.boneMatrixLayout,
            selectionBitsLayout
        };

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
        layoutInfo.pSetLayouts = layouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        vk::PipelineColorBlendAttachmentState noBlend{};
        noBlend.blendEnable = VK_FALSE;
        noBlend.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        std::vector<vk::PipelineColorBlendAttachmentState> blendStates{noBlend};

        // No depth attachment: the fragment shader samples the resolved scene
        // depth and discards occluded fragments itself (3x3 neighborhood
        // tolerance — see frag_selection_mask.glsl for why a fixed-function
        // depth test speckles here).
        core::MeshShaderPipelineConfig config{
            .device = vkDevice,
            .extent = swapChain.getSwapchainExtent(),
            .colorAttachmentFormats = {getMaskFormat()},
            .depthAttachmentFormat = vk::Format::eUndefined,
            .shaderStages = shader->getShaderStages(),
            .existingPipelineLayout = pipelineLayout,
            .cullMode = vk::CullModeFlagBits::eBack,
            .depthTestEnable = false,
            .depthWriteEnable = false,
            .depthCompareOp = vk::CompareOp::eAlways,
            .blendEnable = false,
            .colorBlendAttachments = blendStates
        };
        config.dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};

        auto result = core::PipelineUtilities::createMeshShaderPipeline(config);
        pipeline = result.pipeline;
    }

    void SelectionMaskPipeline::ensureMaskTarget(vk::Extent2D extent)
    {
        if (maskImage && maskExtent.width == extent.width && maskExtent.height == extent.height)
        {
            return;
        }

        destroyMaskTarget();
        maskExtent = extent;

        auto& dev = device.getLogicalDevice();

        core::ImageInfoRequest imageReq(dev, device.getPhysicalDevice());
        imageReq.width = extent.width;
        imageReq.height = extent.height;
        imageReq.format = getMaskFormat();
        imageReq.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
        core::ImageUtilities::createImage(imageReq, maskImage, maskAllocation,
                                          device.getMemoryManager());

        core::ImageViewInfoRequest viewReq(dev, maskImage);
        viewReq.format = getMaskFormat();
        viewReq.aspectFlags = vk::ImageAspectFlagBits::eColor;
        core::ImageUtilities::createImageView(viewReq, maskImageView);

        if (!maskSampler)
        {
            vk::SamplerCreateInfo samplerInfo{};
            samplerInfo.magFilter = vk::Filter::eLinear;
            samplerInfo.minFilter = vk::Filter::eLinear;
            samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
            samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
            maskSampler = dev.createSampler(samplerInfo);
        }
    }

    void SelectionMaskPipeline::writeSelectionBits(const std::vector<uint32_t>& selectedSlots)
    {
        // Every ring entry starts zero-filled, so consecutive empty frames need
        // no work (and no ring advance).
        if (selectedSlots.empty() && lastBitsEmpty)
        {
            return;
        }
        lastBitsEmpty = selectedSlots.empty();

        currentBitsFrame = (currentBitsFrame + 1) % core::MAX_FRAMES_IN_FLIGHT;
        auto& frame = bitsFrames[currentBitsFrame];

        auto* words = static_cast<uint32_t*>(frame.mapped);
        std::memset(words, 0, bitsWordCount * sizeof(uint32_t));
        for (uint32_t slot : selectedSlots)
        {
            uint32_t word = slot >> 5u;
            if (word < bitsWordCount)
            {
                words[word] |= 1u << (slot & 31u);
            }
        }
    }

    void SelectionMaskPipeline::updateSceneDepthInput(vk::ImageView depthImageView)
    {
        if (!depthImageView || depthImageView == sceneDepthView)
        {
            return;
        }

        auto& dev = device.getLogicalDevice();

        if (!depthSampler)
        {
            vk::SamplerCreateInfo samplerInfo{};
            samplerInfo.magFilter = vk::Filter::eNearest;
            samplerInfo.minFilter = vk::Filter::eNearest;
            samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
            samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
            depthSampler = dev.createSampler(samplerInfo);
        }

        // The supplied view exposes only the depth aspect. The frame graph
        // declares ShaderRead, matching this descriptor and fragment access.
        vk::DescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfo.imageView = depthImageView;
        imageInfo.sampler = depthSampler;

        for (auto& frame : bitsFrames)
        {
            vk::WriteDescriptorSet write{};
            write.dstSet = frame.descriptorSet;
            write.dstBinding = 1;
            write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfo;
            dev.updateDescriptorSets(write, nullptr);
        }

        sceneDepthView = depthImageView;
    }

    void SelectionMaskPipeline::beginMaskPass(vk::CommandBuffer cmd) const
    {
        const vk::ClearColorValue clearColor(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});

        core::DynamicRenderingInfo info{};
        info.extent = maskExtent;
        info.colorAttachments = {core::colorClear(maskImageView, clearColor)};

        core::beginDynamicRendering(cmd, info);

        vk::Viewport viewport{0.0f, 0.0f,
            static_cast<float>(maskExtent.width), static_cast<float>(maskExtent.height),
            0.0f, 1.0f};
        cmd.setViewport(0, viewport);

        vk::Rect2D scissor{{0, 0}, maskExtent};
        cmd.setScissor(0, scissor);
    }

    void SelectionMaskPipeline::endMaskPass(vk::CommandBuffer cmd) const
    {
        core::endDynamicRendering(cmd);
    }

    void SelectionMaskPipeline::bindPipeline(vk::CommandBuffer cmd) const
    {
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    }

    void SelectionMaskPipeline::pushConstants(vk::CommandBuffer cmd,
                                              const SelectionMaskPushConstants& pc) const
    {
        cmd.pushConstants(pipelineLayout,
                          vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT |
                          vk::ShaderStageFlagBits::eFragment,
                          0, sizeof(SelectionMaskPushConstants), &pc);
    }

    void SelectionMaskPipeline::destroyMaskTarget()
    {
        auto& dev = device.getLogicalDevice();
        if (maskImageView)
        {
            dev.destroyImageView(maskImageView);
            maskImageView = nullptr;
        }
        if (maskImage)
        {
            dev.destroyImage(maskImage);
            device.getMemoryManager().free(maskAllocation);
            maskAllocation = {};
            maskImage = nullptr;
        }
        maskExtent = vk::Extent2D{};
    }

    void SelectionMaskPipeline::cleanup()
    {
        if (!initialized && !maskImage && !pipeline && !selectionBitsLayout)
        {
            return;
        }

        auto& dev = device.getLogicalDevice();
        dev.waitIdle();

        destroyMaskTarget();

        if (maskSampler)
        {
            dev.destroySampler(maskSampler);
            maskSampler = nullptr;
        }

        sceneDepthView = nullptr;
        if (depthSampler)
        {
            dev.destroySampler(depthSampler);
            depthSampler = nullptr;
        }
        if (pipeline)
        {
            dev.destroyPipeline(pipeline);
            pipeline = nullptr;
        }
        if (pipelineLayout)
        {
            dev.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        for (auto& frame : bitsFrames)
        {
            if (frame.buffer)
            {
                frame.mapped = nullptr;
                core::BufferUtilities::destroyBuffer(dev, frame.buffer, frame.allocation,
                                                     device.getMemoryManager());
                frame.buffer = nullptr;
            }
            frame.descriptorSet = nullptr;
        }

        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (selectionBitsLayout)
        {
            dev.destroyDescriptorSetLayout(selectionBitsLayout);
            selectionBitsLayout = nullptr;
        }

        if (shader)
        {
            shader->cleanUp();
            shader.reset();
        }

        initialized = false;
    }
}
