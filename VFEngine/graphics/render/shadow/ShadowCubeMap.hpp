#pragma once

#include <vulkan/vulkan.hpp>
#include <array>
#include <cstdint>

namespace core
{
    class Device;
}

namespace render::shadow
{
    /**
     * Represents a cube depth texture for point light omnidirectional shadows.
     *
     * Provides:
     * - Per-face image views for framebuffer attachment during shadow pass
     * - Cube view for efficient shader sampling with direction vectors
     * - Layout transitions between depth attachment and shader read states
     *
     * Cube face ordering follows OpenGL/Vulkan convention:
     * 0: +X (right), 1: -X (left), 2: +Y (up), 3: -Y (down), 4: +Z (front), 5: -Z (back)
     */
    class ShadowCubeMap
    {
    public:
        static constexpr uint32_t FACE_COUNT = 6;

        explicit ShadowCubeMap(core::Device& device);
        ~ShadowCubeMap();

        ShadowCubeMap(const ShadowCubeMap&) = delete;
        ShadowCubeMap& operator=(const ShadowCubeMap&) = delete;
        ShadowCubeMap(ShadowCubeMap&&) noexcept;
        ShadowCubeMap& operator=(ShadowCubeMap&&) noexcept;

        /**
         * Initialize the cube depth texture.
         * @param size Width and height of each face in pixels (cube is square)
         * @param format Depth format (default: D32_SFLOAT)
         */
        void init(uint32_t size, vk::Format format = vk::Format::eD32Sfloat);
        void cleanup();
        void recreate(uint32_t size);

        /**
         * Get image view for a specific cube face (for framebuffer attachment).
         * @param face Face index (0-5): +X, -X, +Y, -Y, +Z, -Z
         * @return 2D image view for the specified face
         */
        [[nodiscard]] vk::ImageView getFaceView(uint32_t face) const;

        /**
         * Get cube view for shader sampling.
         * @return Cube image view for samplerCube/samplerCubeShadow
         */
        [[nodiscard]] vk::ImageView getCubeView() const { return cubeView; }

        /**
         * Transition all faces from undefined/shader-read to depth attachment.
         * Call before rendering shadow pass.
         */
        void transitionToDepthAttachment(vk::CommandBuffer cmd);

        /**
         * Transition all faces from depth attachment to shader read.
         * Call after shadow pass, before forward shading.
         */
        void transitionToShaderRead(vk::CommandBuffer cmd);

        /**
         * Transition a single face to depth attachment.
         * Useful for rendering faces individually.
         */
        void transitionFaceToDepthAttachment(vk::CommandBuffer cmd, uint32_t face);

        /**
         * Transition a single face to shader read.
         */
        void transitionFaceToShaderRead(vk::CommandBuffer cmd, uint32_t face);

        // Accessors
        [[nodiscard]] vk::Image getImage() const { return image; }
        [[nodiscard]] vk::Format getFormat() const { return format; }
        [[nodiscard]] uint32_t getSize() const { return size; }
        [[nodiscard]] bool isInitialized() const { return initialized; }
        [[nodiscard]] vk::ImageLayout getCurrentLayout() const { return currentLayout; }

    private:
        core::Device& device;

        // Vulkan resources
        vk::Image image;
        vk::DeviceMemory memory;
        vk::ImageView cubeView;                          // Cube view for shader sampling
        std::array<vk::ImageView, FACE_COUNT> faceViews; // Per-face 2D views for framebuffer

        // Configuration
        uint32_t size = 0;
        vk::Format format = vk::Format::eD32Sfloat;

        // State tracking
        bool initialized = false;
        vk::ImageLayout currentLayout = vk::ImageLayout::eUndefined;

        void createImage();
        void createImageViews();
        void transitionFaces(vk::CommandBuffer cmd, uint32_t baseFace, uint32_t count,
                             vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
    };
}
