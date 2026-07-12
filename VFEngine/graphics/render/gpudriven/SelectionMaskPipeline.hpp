#pragma once

#include "../../core/VulkanMemoryManager.hpp"
#include "../../core/GraphicsConstants.hpp"
#include <vulkan/vulkan.hpp>
#include <array>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
}

namespace render::gpudriven
{
    struct SelectionMaskInitInfo
    {
        uint32_t maxObjectCount = 0;
    };

    // Selection coverage resources used directly by the regular scene shader.
    // Each fragment atomically stores packed depth + selection state into the
    // visibility image after the normal material discard logic has run.
    class SelectionMaskPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        struct BitsFrame
        {
            vk::Buffer buffer;
            core::VulkanAllocation allocation;
            void* mapped = nullptr;
            vk::DescriptorSet descriptorSet;
        };
        std::array<BitsFrame, core::MAX_FRAMES_IN_FLIGHT> bitsFrames{};
        uint32_t currentBitsFrame = 0;
        uint32_t bitsWordCount = 0;
        bool lastBitsEmpty = true;
        vk::DescriptorSetLayout selectionBitsLayout;
        vk::DescriptorPool descriptorPool;

        vk::Image maskImage;
        core::VulkanAllocation maskAllocation;
        vk::ImageView maskImageView;
        vk::Sampler maskSampler;
        vk::Extent2D maskExtent{};

        bool initialized = false;

    public:
        explicit SelectionMaskPipeline(core::Device& device, core::SwapChain& swapChain);
        ~SelectionMaskPipeline();

        SelectionMaskPipeline(const SelectionMaskPipeline&) = delete;
        SelectionMaskPipeline& operator=(const SelectionMaskPipeline&) = delete;

        void init(const SelectionMaskInitInfo& info);
        void cleanup();
        bool isInitialized() const { return initialized; }

        static vk::Format getMaskFormat() { return vk::Format::eR32Uint; }

        void ensureMaskTarget(vk::Extent2D extent);
        void writeSelectionBits(const std::vector<uint32_t>& selectedSlots);
        void clearVisibility(vk::CommandBuffer cmd) const;

        vk::DescriptorSet getCurrentBitsDescriptorSet() const
        {
            return bitsFrames[currentBitsFrame].descriptorSet;
        }
        vk::DescriptorSetLayout getDescriptorSetLayout() const { return selectionBitsLayout; }

        vk::Image getMaskImage() const { return maskImage; }
        vk::ImageView getMaskImageView() const { return maskImageView; }
        vk::Sampler getMaskSampler() const { return maskSampler; }
        vk::Extent2D getMaskExtent() const { return maskExtent; }

    private:
        void createSelectionBitsResources(uint32_t maxObjectCount);
        void updateVisibilityDescriptors();
        void destroyMaskTarget();
    };
}
