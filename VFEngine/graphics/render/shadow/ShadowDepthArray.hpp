#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <cstdint>

namespace core
{
    class Device;
}

namespace render::shadow
{
    /**
     * Represents a depth texture array for Cascaded Shadow Maps (CSM).
     *
     * Provides:
     * - Per-layer image views for framebuffer attachment during shadow pass
     * - Array view for efficient shader sampling with layer indices
     * - Layout transitions between depth attachment and shader read states
     */
    class ShadowDepthArray
    {
    public:
        explicit ShadowDepthArray(core::Device& device);
        ~ShadowDepthArray();

        ShadowDepthArray(const ShadowDepthArray&) = delete;
        ShadowDepthArray& operator=(const ShadowDepthArray&) = delete;
        ShadowDepthArray(ShadowDepthArray&&) noexcept;
        ShadowDepthArray& operator=(ShadowDepthArray&&) noexcept;

        /**
         * Initialize the depth texture array.
         * @param width Width of each layer in pixels
         * @param height Height of each layer in pixels
         * @param layers Number of layers (typically 1-4 for CSM cascades)
         * @param format Depth format (default: D32_SFLOAT)
         */
        void init(uint32_t width, uint32_t height, uint32_t layers,
                  vk::Format format = vk::Format::eD32Sfloat);
        void cleanup();
        void recreate(uint32_t width, uint32_t height, uint32_t layers);

        /**
         * Get image view for a specific layer (for framebuffer attachment).
         * @param layer Layer index (0 to layerCount-1)
         * @return 2D image view for the specified layer
         */
        [[nodiscard]] vk::ImageView getLayerView(uint32_t layer) const;

        /**
         * Get array view for shader sampling.
         * @return 2D array image view containing all layers
         */
        [[nodiscard]] vk::ImageView getArrayView() const { return arrayView; }

        /**
         * Transition all layers from undefined/shader-read to depth attachment.
         * Call before rendering shadow pass.
         */
        void transitionToDepthAttachment(vk::CommandBuffer cmd);

        /**
         * Transition all layers from depth attachment to shader read.
         * Call after shadow pass, before forward shading.
         */
        void transitionToShaderRead(vk::CommandBuffer cmd);

        /**
         * Transition a single layer to depth attachment.
         * Useful for rendering cascades individually.
         */
        void transitionLayerToDepthAttachment(vk::CommandBuffer cmd, uint32_t layer);

        /**
         * Transition a single layer to shader read.
         */
        void transitionLayerToShaderRead(vk::CommandBuffer cmd, uint32_t layer);

        // Accessors
        [[nodiscard]] vk::Image getImage() const { return image; }
        [[nodiscard]] vk::Format getFormat() const { return format; }
        [[nodiscard]] uint32_t getWidth() const { return width; }
        [[nodiscard]] uint32_t getHeight() const { return height; }
        [[nodiscard]] uint32_t getLayerCount() const { return layerCount; }
        [[nodiscard]] bool isInitialized() const { return initialized; }
        [[nodiscard]] vk::ImageLayout getCurrentLayout() const { return currentLayout; }

    private:
        core::Device& device;

        // Vulkan resources
        vk::Image image;
        vk::DeviceMemory memory;
        vk::ImageView arrayView;                // 2D_ARRAY view for shader sampling
        std::vector<vk::ImageView> layerViews;  // Per-layer 2D views for framebuffer

        // Configuration
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t layerCount = 0;
        vk::Format format = vk::Format::eD32Sfloat;

        // State tracking
        bool initialized = false;
        vk::ImageLayout currentLayout = vk::ImageLayout::eUndefined;

        void createImage();
        void createImageViews();
        void transitionLayers(vk::CommandBuffer cmd, uint32_t baseLayer, uint32_t count,
                              vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
    };
}
