#include "CloudRayMarch.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"
#include <cstring>

// Windows defines MemoryBarrier as a macro - undefine it to use vk::MemoryBarrier
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::cloud
{
    CloudRayMarch::CloudRayMarch(core::Device& device, core::SwapChain& swapChain)
        : device{device}, swapChain{swapChain}
    {
    }

    CloudRayMarch::~CloudRayMarch()
    {
        cleanup();
    }

    void CloudRayMarch::init(vk::ImageView shapeNoiseView, vk::ImageView detailNoiseView,
                             vk::ImageView weatherMapView, vk::Sampler noiseSampler,
                             vk::ImageView transmittanceView, vk::Sampler lutSampler,
                             vk::ImageView blueNoiseView)
    {
        auto& dev = device.getLogicalDevice();
        auto& physDev = device.getPhysicalDevice();

        // Compute half-res extent
        auto fullExtent = swapChain.getSwapchainExtent();
        halfExtent.width = std::max(fullExtent.width / 2, 1u);
        halfExtent.height = std::max(fullExtent.height / 2, 1u);

        // --- Create half-res result image (RGBA16F, storage + sampled) ---
        {
            vk::ImageCreateInfo imageInfo{};
            imageInfo.imageType = vk::ImageType::e2D;
            imageInfo.extent = vk::Extent3D{halfExtent.width, halfExtent.height, 1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = vk::Format::eR16G16B16A16Sfloat;
            imageInfo.tiling = vk::ImageTiling::eOptimal;
            imageInfo.initialLayout = vk::ImageLayout::eUndefined;
            imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
            imageInfo.samples = vk::SampleCountFlagBits::e1;
            imageInfo.sharingMode = vk::SharingMode::eExclusive;

            resultImage = dev.createImage(imageInfo);
            vk::MemoryRequirements memReqs = dev.getImageMemoryRequirements(resultImage);
            resultAllocation = device.getMemoryManager().allocate(memReqs, vk::MemoryPropertyFlagBits::eDeviceLocal);
            dev.bindImageMemory(resultImage, resultAllocation.memory, resultAllocation.offset);

            core::ImageViewInfoRequest viewReq(dev, resultImage);
            viewReq.format = vk::Format::eR16G16B16A16Sfloat;
            viewReq.imageType = vk::ImageViewType::e2D;
            viewReq.aspectFlags = vk::ImageAspectFlagBits::eColor;
            core::ImageUtilities::createImageView(viewReq, resultView);
        }

        // --- Create UBO buffer (host-visible, persistently mapped) ---
        {
            core::BufferInfoRequest bufReq(device.getLogicalDevice(), device.getPhysicalDevice());
            bufReq.size = sizeof(GPUCloudParams);
            bufReq.usage = vk::BufferUsageFlagBits::eUniformBuffer;
            bufReq.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(bufReq, paramsBuffer, paramsBufferAllocation, device.getMemoryManager());
            paramsBufferMapped = paramsBufferAllocation.mappedPtr;
        }

        // --- Create descriptor set layout ---
        // Binding 0: result image (storage)
        // Binding 1: shape noise (combined image sampler)
        // Binding 2: detail noise (combined image sampler)
        // Binding 3: weather map (combined image sampler)
        // Binding 4: transmittance LUT (combined image sampler)
        // Binding 5: UBO
        // Binding 6: blue noise (combined image sampler)
        std::vector<vk::DescriptorSetLayoutBinding> bindings{
            {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
            {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
            {2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
            {3, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
            {4, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
            {5, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute},
            {6, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute}
        };

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        dsLayout = dev.createDescriptorSetLayout(layoutInfo);

        // --- Create descriptor pool ---
        std::vector<vk::DescriptorPoolSize> poolSizes{
            {vk::DescriptorType::eStorageImage, 1},
            {vk::DescriptorType::eCombinedImageSampler, 5},
            {vk::DescriptorType::eUniformBuffer, 1}
        };

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        dsPool = dev.createDescriptorPool(poolInfo);

        // --- Allocate descriptor set ---
        vk::DescriptorSetAllocateInfo dsAllocInfo{};
        dsAllocInfo.descriptorPool = dsPool;
        dsAllocInfo.descriptorSetCount = 1;
        dsAllocInfo.pSetLayouts = &dsLayout;
        descriptorSet = dev.allocateDescriptorSets(dsAllocInfo)[0];

        // --- Write descriptors ---
        vk::DescriptorImageInfo resultImgInfo{nullptr, resultView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo shapeNoiseInfo{noiseSampler, shapeNoiseView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo detailNoiseInfo{noiseSampler, detailNoiseView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo weatherMapInfo{noiseSampler, weatherMapView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo transmittanceInfo{lutSampler, transmittanceView, vk::ImageLayout::eGeneral};
        vk::DescriptorBufferInfo bufInfo{paramsBuffer, 0, sizeof(GPUCloudParams)};
        vk::DescriptorImageInfo blueNoiseInfo{noiseSampler, blueNoiseView, vk::ImageLayout::eShaderReadOnlyOptimal};

        std::array<vk::WriteDescriptorSet, 7> writes{};
        writes[0] = {descriptorSet, 0, 0, 1, vk::DescriptorType::eStorageImage, &resultImgInfo};
        writes[1] = {descriptorSet, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &shapeNoiseInfo};
        writes[2] = {descriptorSet, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &detailNoiseInfo};
        writes[3] = {descriptorSet, 3, 0, 1, vk::DescriptorType::eCombinedImageSampler, &weatherMapInfo};
        writes[4] = {descriptorSet, 4, 0, 1, vk::DescriptorType::eCombinedImageSampler, &transmittanceInfo};
        writes[5] = {descriptorSet, 5, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo};
        writes[6] = {descriptorSet, 6, 0, 1, vk::DescriptorType::eCombinedImageSampler, &blueNoiseInfo};
        dev.updateDescriptorSets(writes, nullptr);

        // --- Create compute pipeline ---
        shader = std::make_shared<core::Shader>(device);
        shader->readShader("../../resources/shaders/cloud/cloud_raymarch.glsl");

        vk::PipelineLayoutCreateInfo plInfo{};
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &dsLayout;
        pipelineLayout = dev.createPipelineLayout(plInfo);

        const auto& stages = shader->getShaderStages();
        vk::ComputePipelineCreateInfo cpInfo{};
        cpInfo.stage = stages[0];
        cpInfo.layout = pipelineLayout;
        pipeline = core::PipelineUtilities::createComputePipeline(dev, cpInfo);

        needsInitialTransition = true;
        initialized = true;
    }

    void CloudRayMarch::cleanup()
    {
        if (!initialized)
            return;

        auto& dev = device.getLogicalDevice();

        // Destroy pipeline
        if (pipeline) { dev.destroyPipeline(pipeline); pipeline = nullptr; }
        if (pipelineLayout) { dev.destroyPipelineLayout(pipelineLayout); pipelineLayout = nullptr; }

        // Destroy descriptor pool/layout
        if (dsPool) { dev.destroyDescriptorPool(dsPool); dsPool = nullptr; }
        if (dsLayout) { dev.destroyDescriptorSetLayout(dsLayout); dsLayout = nullptr; }

        // Destroy UBO
        paramsBufferMapped = nullptr;
        if (paramsBuffer) { core::BufferUtilities::destroyBuffer(dev, paramsBuffer, paramsBufferAllocation, device.getMemoryManager()); }

        // Destroy result image
        if (resultView) { dev.destroyImageView(resultView); resultView = nullptr; }
        if (resultImage) { dev.destroyImage(resultImage); resultImage = nullptr; }
        if (resultAllocation.isValid()) { device.getMemoryManager().free(resultAllocation); resultAllocation = {}; }

        // Clean up shader
        if (shader) { shader->cleanUp(); shader.reset(); }

        initialized = false;
    }

    void CloudRayMarch::recreate(vk::ImageView shapeNoiseView, vk::ImageView detailNoiseView,
                                 vk::ImageView weatherMapView, vk::Sampler noiseSampler,
                                 vk::ImageView transmittanceView, vk::Sampler lutSampler,
                                 vk::ImageView blueNoiseView)
    {
        if (!initialized) return;

        auto& dev = device.getLogicalDevice();

        // Recompute half extent
        auto fullExtent = swapChain.getSwapchainExtent();
        halfExtent.width = std::max(fullExtent.width / 2, 1u);
        halfExtent.height = std::max(fullExtent.height / 2, 1u);

        // Destroy old result image
        if (resultView) { dev.destroyImageView(resultView); resultView = nullptr; }
        if (resultImage) { dev.destroyImage(resultImage); resultImage = nullptr; }
        if (resultAllocation.isValid()) { device.getMemoryManager().free(resultAllocation); resultAllocation = {}; }

        // Recreate result image at new size
        {
            vk::ImageCreateInfo imageInfo{};
            imageInfo.imageType = vk::ImageType::e2D;
            imageInfo.extent = vk::Extent3D{halfExtent.width, halfExtent.height, 1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = vk::Format::eR16G16B16A16Sfloat;
            imageInfo.tiling = vk::ImageTiling::eOptimal;
            imageInfo.initialLayout = vk::ImageLayout::eUndefined;
            imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
            imageInfo.samples = vk::SampleCountFlagBits::e1;
            imageInfo.sharingMode = vk::SharingMode::eExclusive;

            resultImage = dev.createImage(imageInfo);
            vk::MemoryRequirements memReqs = dev.getImageMemoryRequirements(resultImage);
            resultAllocation = device.getMemoryManager().allocate(memReqs, vk::MemoryPropertyFlagBits::eDeviceLocal);
            dev.bindImageMemory(resultImage, resultAllocation.memory, resultAllocation.offset);

            core::ImageViewInfoRequest viewReq(dev, resultImage);
            viewReq.format = vk::Format::eR16G16B16A16Sfloat;
            viewReq.imageType = vk::ImageViewType::e2D;
            viewReq.aspectFlags = vk::ImageAspectFlagBits::eColor;
            core::ImageUtilities::createImageView(viewReq, resultView);
        }

        // Rebuild descriptor pool and set (pool reset frees old set)
        if (dsPool) { dev.destroyDescriptorPool(dsPool); dsPool = nullptr; }

        std::vector<vk::DescriptorPoolSize> poolSizes{
            {vk::DescriptorType::eStorageImage, 1},
            {vk::DescriptorType::eCombinedImageSampler, 5},
            {vk::DescriptorType::eUniformBuffer, 1}
        };

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        dsPool = dev.createDescriptorPool(poolInfo);

        vk::DescriptorSetAllocateInfo dsAllocInfo{};
        dsAllocInfo.descriptorPool = dsPool;
        dsAllocInfo.descriptorSetCount = 1;
        dsAllocInfo.pSetLayouts = &dsLayout;
        descriptorSet = dev.allocateDescriptorSets(dsAllocInfo)[0];

        // Update descriptors with new result image and (possibly updated) external views
        vk::DescriptorImageInfo resultImgInfo{nullptr, resultView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo shapeNoiseInfo{noiseSampler, shapeNoiseView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo detailNoiseInfo{noiseSampler, detailNoiseView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo weatherMapInfo{noiseSampler, weatherMapView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo transmittanceInfo{lutSampler, transmittanceView, vk::ImageLayout::eGeneral};
        vk::DescriptorBufferInfo bufInfoDesc{paramsBuffer, 0, sizeof(GPUCloudParams)};
        vk::DescriptorImageInfo blueNoiseInfo{noiseSampler, blueNoiseView, vk::ImageLayout::eShaderReadOnlyOptimal};

        std::array<vk::WriteDescriptorSet, 7> writes{};
        writes[0] = {descriptorSet, 0, 0, 1, vk::DescriptorType::eStorageImage, &resultImgInfo};
        writes[1] = {descriptorSet, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &shapeNoiseInfo};
        writes[2] = {descriptorSet, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &detailNoiseInfo};
        writes[3] = {descriptorSet, 3, 0, 1, vk::DescriptorType::eCombinedImageSampler, &weatherMapInfo};
        writes[4] = {descriptorSet, 4, 0, 1, vk::DescriptorType::eCombinedImageSampler, &transmittanceInfo};
        writes[5] = {descriptorSet, 5, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfoDesc};
        writes[6] = {descriptorSet, 6, 0, 1, vk::DescriptorType::eCombinedImageSampler, &blueNoiseInfo};
        dev.updateDescriptorSets(writes, nullptr);

        needsInitialTransition = true;
    }

    void CloudRayMarch::updateParams(const GPUCloudParams& params)
    {
        if (!paramsBufferMapped) return;
        std::memcpy(paramsBufferMapped, &params, sizeof(GPUCloudParams));
    }

    void CloudRayMarch::dispatch(const vk::CommandBuffer& cmd)
    {
        if (!initialized) return;

        // Transition result image from UNDEFINED to GENERAL on first use
        if (needsInitialTransition)
        {
            core::ImageUtilities::transitionImageLayout(cmd, resultImage,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
                vk::ImageAspectFlagBits::eColor);
            needsInitialTransition = false;
        }

        // Memory barrier: ensure UBO writes are visible to shader
        vk::MemoryBarrier hostBarrier{vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eShaderRead};
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eComputeShader,
                            {}, hostBarrier, {}, {});

        // Bind and dispatch
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, descriptorSet, nullptr);
        cmd.dispatch((halfExtent.width + 15) / 16, (halfExtent.height + 15) / 16, 1);

        // Memory barrier: ensure compute writes are visible for subsequent reads
        vk::MemoryBarrier computeBarrier{vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead};
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader,
                            {}, computeBarrier, {}, {});
    }
}
