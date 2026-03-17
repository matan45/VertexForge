#include "CloudTemporal.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Log.hpp"
#include <cstring>

// Windows defines MemoryBarrier as a macro - undefine it to use vk::MemoryBarrier
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::cloud
{
    CloudTemporal::CloudTemporal(core::Device& device, core::SwapChain& swapChain)
        : device(device), swapChain(swapChain)
    {
    }

    CloudTemporal::~CloudTemporal()
    {
        cleanup();
    }

    void CloudTemporal::init(vk::ImageView currentResultView, vk::Image currentResultImage)
    {
        if (initialized)
        {
            vfLogWarning("CloudTemporal: Already initialized");
            return;
        }

        auto fullExtent = swapChain.getSwapchainExtent();
        halfExtent.width = std::max(1u, fullExtent.width / 2);
        halfExtent.height = std::max(1u, fullExtent.height / 2);

        // Create history image (RGBA16F, half-res)
        core::ImageInfoRequest imageReq(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            halfExtent.width, halfExtent.height,
            1, // layers
            1, // mipLevels
            vk::Format::eR16G16B16A16Sfloat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(imageReq, historyImage, historyMemory);

        core::ImageViewInfoRequest viewReq(
            device.getLogicalDevice(),
            historyImage,
            vk::Format::eR16G16B16A16Sfloat,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D,
            1, // layerCount
            1  // mipLevels
        );
        core::ImageUtilities::createImageView(viewReq, historyView);

        // Create UBO buffer (host-visible, persistently mapped)
        core::BufferInfoRequest bufReq(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            sizeof(CloudTemporalUBO),
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        core::BufferUtilities::createBuffer(bufReq, paramsBuffer, paramsBufferMemory);
        paramsBufferMapped = device.getLogicalDevice().mapMemory(paramsBufferMemory, 0, sizeof(CloudTemporalUBO));

        // Create descriptor set layout
        // Binding 0: currentResult (storage image, read-write)
        // Binding 1: historyResult (storage image, read-write)
        // Binding 2: UBO
        std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageImage;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageImage;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        dsLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);

        // Create descriptor pool
        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eStorageImage;
        poolSizes[0].descriptorCount = 2;
        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        dsPool = device.getLogicalDevice().createDescriptorPool(poolInfo);

        // Allocate descriptor set
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = dsPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &dsLayout;
        descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        // Write descriptors
        vk::DescriptorImageInfo currentImageInfo{};
        currentImageInfo.imageView = currentResultView;
        currentImageInfo.imageLayout = vk::ImageLayout::eGeneral;

        vk::DescriptorImageInfo historyImageInfo{};
        historyImageInfo.imageView = historyView;
        historyImageInfo.imageLayout = vk::ImageLayout::eGeneral;

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = paramsBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(CloudTemporalUBO);

        std::array<vk::WriteDescriptorSet, 3> writes{};

        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eStorageImage;
        writes[0].pImageInfo = &currentImageInfo;

        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageImage;
        writes[1].pImageInfo = &historyImageInfo;

        writes[2].dstSet = descriptorSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[2].pBufferInfo = &bufferInfo;

        device.getLogicalDevice().updateDescriptorSets(writes, {});

        // Create pipeline layout
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &dsLayout;
        pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);

        // Create compute pipeline
        shader = std::make_shared<core::Shader>(device);
        shader->readShader("../../resources/shaders/cloud/cloud_temporal.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            vfLogError("CloudTemporal: Failed to load shader: {}", shader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        auto result = device.getLogicalDevice().createComputePipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            vfLogError("CloudTemporal: Failed to create compute pipeline");
            return;
        }

        pipeline = result.value;
        needsInitialTransition = true;
        initialized = true;

        vfLogInfo("CloudTemporal: Initialized ({}x{})", halfExtent.width, halfExtent.height);
    }

    void CloudTemporal::cleanup()
    {
        if (!initialized)
            return;

        auto& dev = device.getLogicalDevice();
        dev.waitIdle();

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

        if (shader)
        {
            shader->cleanUp();
            shader.reset();
        }

        if (dsPool)
        {
            dev.destroyDescriptorPool(dsPool);
            dsPool = nullptr;
        }

        if (dsLayout)
        {
            dev.destroyDescriptorSetLayout(dsLayout);
            dsLayout = nullptr;
        }

        if (paramsBufferMapped)
        {
            dev.unmapMemory(paramsBufferMemory);
            paramsBufferMapped = nullptr;
        }

        if (paramsBuffer)
        {
            core::BufferUtilities::destroyBuffer(dev, paramsBuffer, paramsBufferMemory);
        }

        if (historyView)
        {
            dev.destroyImageView(historyView);
            historyView = nullptr;
        }

        if (historyImage)
        {
            dev.destroyImage(historyImage);
            historyImage = nullptr;
        }

        if (historyMemory)
        {
            dev.freeMemory(historyMemory);
            historyMemory = nullptr;
        }

        initialized = false;
    }

    void CloudTemporal::recreate(vk::ImageView currentResultView, vk::Image currentResultImage)
    {
        cleanup();
        init(currentResultView, currentResultImage);
    }

    void CloudTemporal::updateParams(const CloudTemporalUBO& params)
    {
        if (!paramsBufferMapped)
            return;

        std::memcpy(paramsBufferMapped, &params, sizeof(CloudTemporalUBO));
    }

    void CloudTemporal::dispatch(const vk::CommandBuffer& cmd)
    {
        if (!initialized || !pipeline)
            return;

        // Transition history image to General on first use
        if (needsInitialTransition)
        {
            vk::ImageMemoryBarrier barrier{};
            barrier.oldLayout = vk::ImageLayout::eUndefined;
            barrier.newLayout = vk::ImageLayout::eGeneral;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = historyImage;
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = vk::AccessFlagBits::eNone;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, {}, barrier
            );

            needsInitialTransition = false;
        }

        // UBO host-write barrier
        vk::MemoryBarrier uboBarrier{vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eShaderRead};
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eHost,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, uboBarrier, {}, {}
        );

        // Bind and dispatch
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute, pipelineLayout, 0,
            1, &descriptorSet,
            0, nullptr
        );

        uint32_t groupsX = (halfExtent.width + 15) / 16;
        uint32_t groupsY = (halfExtent.height + 15) / 16;
        cmd.dispatch(groupsX, groupsY, 1);

        // Post-dispatch memory barrier for history image writes
        vk::MemoryBarrier computeBarrier{vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead};
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eFragmentShader,
            {}, computeBarrier, {}, {}
        );
    }
}
