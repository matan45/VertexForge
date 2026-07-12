#include "SelectionMaskPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
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
        initialized = selectionBitsLayout != nullptr;
        if (initialized)
        {
            vfLogInfo("Selection coverage resources initialized ({} bitmask words)", bitsWordCount);
        }
    }

    void SelectionMaskPipeline::createSelectionBitsResources(uint32_t maxObjectCount)
    {
        auto& dev = device.getLogicalDevice();

        bitsWordCount = (maxObjectCount + 31u) / 32u;
        if (bitsWordCount == 0) bitsWordCount = 1;

        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageImage;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        selectionBitsLayout = dev.createDescriptorSetLayout(layoutInfo);

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eStorageBuffer, core::MAX_FRAMES_IN_FLIGHT};
        poolSizes[1] = {vk::DescriptorType::eStorageImage, core::MAX_FRAMES_IN_FLIGHT};

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

            vk::DescriptorBufferInfo bufferInfo{frame.buffer, 0, bufReq.size};
            vk::WriteDescriptorSet write{};
            write.dstSet = frame.descriptorSet;
            write.dstBinding = 0;
            write.descriptorType = vk::DescriptorType::eStorageBuffer;
            write.descriptorCount = 1;
            write.pBufferInfo = &bufferInfo;
            dev.updateDescriptorSets(write, nullptr);
        }
    }

    void SelectionMaskPipeline::ensureMaskTarget(vk::Extent2D extent)
    {
        if (maskImage && maskExtent == extent) return;

        destroyMaskTarget();
        maskExtent = extent;
        auto& dev = device.getLogicalDevice();

        core::ImageInfoRequest imageReq(dev, device.getPhysicalDevice());
        imageReq.width = extent.width;
        imageReq.height = extent.height;
        imageReq.format = getMaskFormat();
        imageReq.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                         vk::ImageUsageFlagBits::eTransferDst;
        core::ImageUtilities::createImage(imageReq, maskImage, maskAllocation,
                                          device.getMemoryManager());

        core::ImageViewInfoRequest viewReq(dev, maskImage);
        viewReq.format = getMaskFormat();
        viewReq.aspectFlags = vk::ImageAspectFlagBits::eColor;
        core::ImageUtilities::createImageView(viewReq, maskImageView);

        // VK-1490: the storage-image descriptor (set 15, binding 1) is statically used
        // by every mesh pipeline and bound on every scene draw, so the mask must sit in
        // its declared eGeneral layout at all times — including no-selection and RTT
        // frames the frame graph never touches. All operations on it are valid in
        // eGeneral (clearColorImage, imageAtomicMin, usampler2D sampling), so transition
        // it once here and keep it there (the frame graph's per-pass usages resolve to
        // eGeneral / bracket the transfer clear and return to eGeneral each frame).
        {
            auto cmd = core::Utilities::beginSingleTimeCommands(dev, device.getStagingCommandPool());
            core::ImageUtilities::transitionImageLayout(
                cmd.get(), maskImage,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
                vk::ImageAspectFlagBits::eColor);
            core::Utilities::endSingleTimeCommands(device, cmd);
        }

        if (!maskSampler)
        {
            vk::SamplerCreateInfo samplerInfo{};
            samplerInfo.magFilter = vk::Filter::eNearest;
            samplerInfo.minFilter = vk::Filter::eNearest;
            samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
            samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
            maskSampler = dev.createSampler(samplerInfo);
        }

        updateVisibilityDescriptors();
    }

    void SelectionMaskPipeline::updateVisibilityDescriptors()
    {
        if (!maskImageView) return;

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = vk::ImageLayout::eGeneral;
        imageInfo.imageView = maskImageView;

        for (auto& frame : bitsFrames)
        {
            vk::WriteDescriptorSet write{};
            write.dstSet = frame.descriptorSet;
            write.dstBinding = 1;
            write.descriptorType = vk::DescriptorType::eStorageImage;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfo;
            device.getLogicalDevice().updateDescriptorSets(write, nullptr);
        }
    }

    void SelectionMaskPipeline::writeSelectionBits(const std::vector<uint32_t>& selectedSlots)
    {
        if (selectedSlots.empty() && lastBitsEmpty) return;
        lastBitsEmpty = selectedSlots.empty();

        currentBitsFrame = (currentBitsFrame + 1) % core::MAX_FRAMES_IN_FLIGHT;
        auto& frame = bitsFrames[currentBitsFrame];
        auto* words = static_cast<uint32_t*>(frame.mapped);
        std::memset(words, 0, bitsWordCount * sizeof(uint32_t));
        for (uint32_t slot : selectedSlots)
        {
            const uint32_t word = slot >> 5u;
            if (word < bitsWordCount) words[word] |= 1u << (slot & 31u);
        }
    }

    void SelectionMaskPipeline::clearVisibility(vk::CommandBuffer cmd) const
    {
        const vk::ClearColorValue clear(std::array<uint32_t, 4>{0xFFFFFFFFu, 0u, 0u, 0u});
        const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        cmd.clearColorImage(maskImage, vk::ImageLayout::eTransferDstOptimal, clear, range);
    }

    void SelectionMaskPipeline::destroyMaskTarget()
    {
        auto& dev = device.getLogicalDevice();
        if (maskImageView) dev.destroyImageView(maskImageView);
        if (maskImage)
        {
            dev.destroyImage(maskImage);
            device.getMemoryManager().free(maskAllocation);
        }
        maskImageView = nullptr;
        maskImage = nullptr;
        maskAllocation = {};
        maskExtent = vk::Extent2D{};
    }

    void SelectionMaskPipeline::cleanup()
    {
        if (!selectionBitsLayout && !maskImage) return;

        auto& dev = device.getLogicalDevice();
        dev.waitIdle();
        destroyMaskTarget();
        if (maskSampler) dev.destroySampler(maskSampler);
        maskSampler = nullptr;

        for (auto& frame : bitsFrames)
        {
            if (frame.buffer)
                core::BufferUtilities::destroyBuffer(dev, frame.buffer, frame.allocation,
                                                     device.getMemoryManager());
            frame = {};
        }
        if (descriptorPool) dev.destroyDescriptorPool(descriptorPool);
        if (selectionBitsLayout) dev.destroyDescriptorSetLayout(selectionBitsLayout);
        descriptorPool = nullptr;
        selectionBitsLayout = nullptr;
        initialized = false;
    }
}
