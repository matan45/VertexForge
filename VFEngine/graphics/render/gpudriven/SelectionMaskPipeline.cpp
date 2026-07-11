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

        // Set 6: one readonly storage buffer visible to the task stage.
        vk::DescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = vk::DescriptorType::eStorageBuffer;
        binding.descriptorCount = 1;
        binding.stageFlags = vk::ShaderStageFlagBits::eTaskEXT;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        selectionBitsLayout = dev.createDescriptorSetLayout(layoutInfo);

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = core::MAX_FRAMES_IN_FLIGHT;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = core::MAX_FRAMES_IN_FLIGHT;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
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

        // Depth: test-only LessOrEqual against the resolved scene depth — the
        // selected geometry re-rasterizes with the identical transform chain, so
        // its visible surface passes and occluded fragments fail.
        core::MeshShaderPipelineConfig config{
            .device = vkDevice,
            .extent = swapChain.getSwapchainExtent(),
            .colorAttachmentFormats = {getMaskFormat()},
            .depthAttachmentFormat = info.depthFormat,
            .shaderStages = shader->getShaderStages(),
            .existingPipelineLayout = pipelineLayout,
            .cullMode = vk::CullModeFlagBits::eBack,
            .depthTestEnable = true,
            .depthWriteEnable = false,
            .depthCompareOp = vk::CompareOp::eLessOrEqual,
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

    vk::DescriptorSet SelectionMaskPipeline::updateSelectionBits(
        const std::vector<uint32_t>& selectedSlots)
    {
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
        return frame.descriptorSet;
    }

    void SelectionMaskPipeline::beginMaskPass(vk::CommandBuffer cmd, vk::ImageView sceneDepthView) const
    {
        const vk::ClearColorValue clearColor(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});

        core::DynamicRenderingInfo info{};
        info.extent = maskExtent;
        info.colorAttachments = {core::colorClear(maskImageView, clearColor)};
        info.depthAttachment = core::depthReadOnly(sceneDepthView);

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
