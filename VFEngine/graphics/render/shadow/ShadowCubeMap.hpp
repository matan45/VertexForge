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
         *
         * WARNING: Per-face transitions do NOT update currentLayout tracking.
         * After using per-face transitions, the global layout state becomes undefined.
         * You must either:
         * - Use only per-face transitions and manage synchronization manually
         * - Call transitionToDepthAttachment/transitionToShaderRead to reset to a known state
         *
         * @param cmd Command buffer to record transition
         * @param face Face index to transition (0-5)
         * @param assumedCurrentLayout The layout to transition FROM (caller must track this)
         */
        void transitionFaceToDepthAttachment(vk::CommandBuffer cmd, uint32_t face,
                                              vk::ImageLayout assumedCurrentLayout);

        /**
         * Transition a single face to shader read.
         * See transitionFaceToDepthAttachment for warnings about per-face transitions.
         */
        void transitionFaceToShaderRead(vk::CommandBuffer cmd, uint32_t face,
                                         vk::ImageLayout assumedCurrentLayout);

        // Accessors
        [[nodiscard]] vk::Image getImage() const { return image; }
        [[nodiscard]] vk::DeviceMemory getMemory() const { return memory; }
        [[nodiscard]] vk::Format getFormat() const { return format; }
        [[nodiscard]] uint32_t getSize() const { return size; }
        [[nodiscard]] bool isInitialized() const { return initialized; }
        [[nodiscard]] vk::ImageLayout getCurrentLayout() const { return currentLayout; }

        /**
         * Get or create a cached framebuffer for a specific cube face.
         * Framebuffers are cached per face and only recreated if the render pass changes.
         * @param face Face index (0-5)
         * @param renderPass The render pass to create framebuffer for
         * @param logicalDevice The Vulkan device
         * @return Cached or newly created framebuffer for the face
         */
        [[nodiscard]] vk::Framebuffer getOrCreateFramebuffer(uint32_t face, vk::RenderPass renderPass,
                                                              const vk::Device& logicalDevice);

        /**
         * Extract resources for deferred deletion (transfers ownership).
         * After calling this, the ShadowCubeMap is left in an uninitialized state.
         * The caller is responsible for destroying the returned resources.
         */
        struct ExtractedResources
        {
            vk::Image image;
            vk::DeviceMemory memory;
            vk::ImageView cubeView;
            std::array<vk::ImageView, FACE_COUNT> faceViews;
            std::array<vk::Framebuffer, FACE_COUNT> framebuffers;
        };
        [[nodiscard]] ExtractedResources extractResources();

    private:
        core::Device& device;

        // Vulkan resources
        vk::Image image;
        vk::DeviceMemory memory;
        vk::ImageView cubeView;                          // Cube view for shader sampling
        std::array<vk::ImageView, FACE_COUNT> faceViews; // Per-face 2D views for framebuffer

        // Cached framebuffers (one per face)
        std::array<vk::Framebuffer, FACE_COUNT> cachedFramebuffers{};
        vk::RenderPass cachedRenderPass{};  // Track which render pass framebuffers were created for

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
