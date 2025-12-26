#include "HiZBuffer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/Shader.hpp"
#include "print/Logger.hpp"
#include <algorithm>
#include <cmath>

namespace render::occlusion
{
    HiZBuffer::HiZBuffer(core::Device& device, core::SwapChain& swapChain)
        : device(device), swapChain(swapChain)
    {
    }

    HiZBuffer::~HiZBuffer()
    {
        cleanup();
    }

    void HiZBuffer::init(vk::Image depthImage, vk::ImageView depthImageView, vk::Format format)
    {
        sourceDepthImage = depthImage;
        sourceDepthView = depthImageView;
        depthFormat = format;
        width = swapChain.getSwapchainExtent().width;
        height = swapChain.getSwapchainExtent().height;
        
        mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

        createHiZImage();
        createHiZSampler();
        createComputePipeline();
        createDescriptorSets();

        initialized = true;
        loggerInfo("Hi-Z buffer initialized: {}x{} with {} mip levels", width, height, mipLevels);
    }

    void HiZBuffer::createHiZImage()
    {
        // Create Hi-Z image using utilities
        core::ImageInfoRequest imageRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            width, height,
            1,  // layers
            mipLevels,
            vk::Format::eR32Sfloat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::Utilities::createImage(imageRequest, hiZImage, hiZMemory);

        // Create full mip chain view using utilities
        core::ImageViewInfoRequest viewRequest(
            device.getLogicalDevice(),
            hiZImage,
            vk::Format::eR32Sfloat,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D,
            1,  // layerCount
            mipLevels
        );
        core::Utilities::createImageView(viewRequest, hiZImageView);

        // Create per-mip views for compute shader (need manual creation for baseMipLevel)
        mipViews.resize(mipLevels);
        for (uint32_t i = 0; i < mipLevels; ++i)
        {
            vk::ImageViewCreateInfo mipViewInfo{};
            mipViewInfo.image = hiZImage;
            mipViewInfo.viewType = vk::ImageViewType::e2D;
            mipViewInfo.format = vk::Format::eR32Sfloat;
            mipViewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            mipViewInfo.subresourceRange.baseMipLevel = i;
            mipViewInfo.subresourceRange.levelCount = 1;
            mipViewInfo.subresourceRange.baseArrayLayer = 0;
            mipViewInfo.subresourceRange.layerCount = 1;
            mipViews[i] = device.getLogicalDevice().createImageView(mipViewInfo);
        }

        // Transition all mip levels to ShaderReadOnlyOptimal so descriptors are valid before first generate()
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
        vk::CommandPool tempPool = device.getLogicalDevice().createCommandPool(poolInfo);

        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.commandPool = tempPool;
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = 1;
        vk::CommandBuffer cmd = device.getLogicalDevice().allocateCommandBuffers(allocInfo)[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmd.begin(beginInfo);

        vk::ImageMemoryBarrier barrier{};
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = hiZImage;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = vk::AccessFlagBits::eNone;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, {}, barrier
        );

        cmd.end();

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        device.getGraphicsQueue().submit(submitInfo);
        device.getGraphicsQueue().waitIdle();

        device.getLogicalDevice().destroyCommandPool(tempPool);
    }

    void HiZBuffer::createHiZSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eNearest; // Point sampling for Hi-Z
        samplerInfo.minFilter = vk::Filter::eNearest;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels);

        hiZSampler = device.getLogicalDevice().createSampler(samplerInfo);
        
        vk::SamplerCreateInfo depthSamplerInfo = samplerInfo;
        depthSamplerInfo.maxLod = 0.0f;
        depthSampler = device.getLogicalDevice().createSampler(depthSamplerInfo);
    }

    void HiZBuffer::createComputePipeline()
    {
        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/hiz/hiz_generate.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            loggerError("Failed to load Hi-Z compute shader: {}", shader->getLastCompilationError());
            return;
        }
        
        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
        
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;
        
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageImage;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
        
        vk::PushConstantRange pushConstant{};
        pushConstant.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushConstant.offset = 0;
        pushConstant.size = sizeof(int32_t) * 6; // outputSize(2) + inputSize(2) + isFirstMip + padding
        
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
        pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);
        
        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        auto pipelineResult = device.getLogicalDevice().createComputePipeline(nullptr, pipelineInfo);
        computePipeline = pipelineResult.value;
    }

    void HiZBuffer::createDescriptorSets()
    {
        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = mipLevels;
        poolSizes[1].type = vk::DescriptorType::eStorageImage;
        poolSizes[1].descriptorCount = mipLevels;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = mipLevels;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
        
        std::vector<vk::DescriptorSetLayout> layouts(mipLevels, descriptorSetLayout);
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = mipLevels;
        allocInfo.pSetLayouts = layouts.data();
        descriptorSets = device.getLogicalDevice().allocateDescriptorSets(allocInfo);
        
        for (uint32_t i = 0; i < mipLevels; ++i)
        {
            vk::DescriptorImageInfo inputInfo{};
            inputInfo.sampler = (i == 0) ? depthSampler : hiZSampler;
            inputInfo.imageView = (i == 0) ? sourceDepthView : mipViews[i - 1];
            inputInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            vk::DescriptorImageInfo outputInfo{};
            outputInfo.imageView = mipViews[i];
            outputInfo.imageLayout = vk::ImageLayout::eGeneral;

            std::array<vk::WriteDescriptorSet, 2> writes{};

            writes[0].dstSet = descriptorSets[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[0].pImageInfo = &inputInfo;

            writes[1].dstSet = descriptorSets[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = vk::DescriptorType::eStorageImage;
            writes[1].pImageInfo = &outputInfo;

            device.getLogicalDevice().updateDescriptorSets(writes, {});
        }
    }

    void HiZBuffer::generate(vk::CommandBuffer cmd)
    {
        if (!initialized) return;
        
        {
            vk::ImageMemoryBarrier depthBarrier{};
            depthBarrier.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            depthBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depthBarrier.image = sourceDepthImage;
            depthBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth |
                vk::ImageAspectFlagBits::eStencil;
            depthBarrier.subresourceRange.baseMipLevel = 0;
            depthBarrier.subresourceRange.levelCount = 1;
            depthBarrier.subresourceRange.baseArrayLayer = 0;
            depthBarrier.subresourceRange.layerCount = 1;
            depthBarrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            depthBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eLateFragmentTests,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, {}, depthBarrier
            );
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);

        uint32_t mipWidth = width;
        uint32_t mipHeight = height;

        for (uint32_t i = 0; i < mipLevels; ++i)
        {
            uint32_t outputWidth = std::max(1u, mipWidth / 2);
            uint32_t outputHeight = std::max(1u, mipHeight / 2);

            if (i == 0)
            {
                outputWidth = mipWidth;
                outputHeight = mipHeight;
            }

            // Transition output mip to general for writing
            vk::ImageMemoryBarrier barrier{};
            barrier.oldLayout = vk::ImageLayout::eUndefined;
            barrier.newLayout = vk::ImageLayout::eGeneral;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = hiZImage;
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            barrier.subresourceRange.baseMipLevel = i;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = vk::AccessFlagBits::eNone;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, {}, barrier
            );

            // Bind descriptor set and push constants
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout,
                                   0, descriptorSets[i], {});
            
            struct HiZPushConstants
            {
                int32_t outputWidth;
                int32_t outputHeight;
                int32_t inputWidth;
                int32_t inputHeight;
                int32_t isFirstMip;
                int32_t padding;
            } pushData;
            pushData.outputWidth = static_cast<int32_t>(outputWidth);
            pushData.outputHeight = static_cast<int32_t>(outputHeight);
            pushData.inputWidth = static_cast<int32_t>(mipWidth);
            pushData.inputHeight = static_cast<int32_t>(mipHeight);
            pushData.isFirstMip = (i == 0) ? 1 : 0;
            pushData.padding = 0;

            cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute,
                              0, sizeof(pushData), &pushData);
            
            uint32_t groupsX = (outputWidth + 7) / 8;
            uint32_t groupsY = (outputHeight + 7) / 8;
            cmd.dispatch(groupsX, groupsY, 1);
            
            barrier.oldLayout = vk::ImageLayout::eGeneral;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, {}, barrier
            );

            mipWidth = outputWidth;
            mipHeight = outputHeight;
        }

        // Transition depth buffer back to depth attachment layout for next frame's rendering
        {
            vk::ImageMemoryBarrier depthBarrier{};
            depthBarrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            depthBarrier.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depthBarrier.image = sourceDepthImage;
            depthBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth |
                vk::ImageAspectFlagBits::eStencil;
            depthBarrier.subresourceRange.baseMipLevel = 0;
            depthBarrier.subresourceRange.levelCount = 1;
            depthBarrier.subresourceRange.baseArrayLayer = 0;
            depthBarrier.subresourceRange.layerCount = 1;
            depthBarrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
            depthBarrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                vk::AccessFlagBits::eDepthStencilAttachmentWrite;

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eEarlyFragmentTests,
                {}, {}, {}, depthBarrier
            );
        }
    }

    void HiZBuffer::cleanup()
    {
        if (!initialized) return;

        device.getLogicalDevice().waitIdle();

        device.getLogicalDevice().destroyDescriptorPool(descriptorPool);
        device.getLogicalDevice().destroyPipeline(computePipeline);
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
        device.getLogicalDevice().destroyDescriptorSetLayout(descriptorSetLayout);

        device.getLogicalDevice().destroySampler(hiZSampler);
        device.getLogicalDevice().destroySampler(depthSampler);

        for (auto& view : mipViews)
        {
            device.getLogicalDevice().destroyImageView(view);
        }
        device.getLogicalDevice().destroyImageView(hiZImageView);
        device.getLogicalDevice().freeMemory(hiZMemory);
        device.getLogicalDevice().destroyImage(hiZImage);

        if (shader)
        {
            shader->cleanUp();
            shader.reset();
        }

        initialized = false;
    }
}
