#pragma once

#include "../../../core/ImageUtilities.hpp"
#include "../../../core/PerFrameBuffer.hpp"
#include "../../../core/VulkanMemoryManager.hpp"

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace core
{
    class Device;
}

namespace render::gpudriven
{
    // Mirrors TerrainWeatherMaskUBO in mesh_terrain.glsl. 32 bytes, std140-friendly.
    struct TerrainWeatherMaskUBOData
    {
        float worldMinX = 0.0f;
        float worldMinZ = 0.0f;
        float worldMaxX = 0.0f;
        float worldMaxZ = 0.0f;
        float wetnessScale = 1.0f;
        float snowScale = 1.0f;
        uint32_t flags = 0; // bit0 = enabled
        float pad = 0.0f;
    };
    static_assert(sizeof(TerrainWeatherMaskUBOData) == 32,
                  "mirrored by TerrainWeatherMaskUBO in mesh_terrain.glsl");

    inline constexpr uint32_t TERRAIN_WEATHER_MASK_FLAG_ENABLED = 1u << 0;

    // VK-1614 — the GPU side of the world-anchored wetness/snow mask (set 11, bindings 5/6).
    //
    // Terrain-owned rather than reusing PluginTextureManager's world mask: that one is single-slot,
    // single-channel, plugin-lifetime and already spoken for by fog of war.
    //
    // The dummy image and the zeroed params UBO are NOT an optimisation, they are a correctness
    // requirement: PipelineUtilities::createUpdateAfterBindLayout sets only eUpdateAfterBind and
    // never ePartiallyBound, so a binding that is statically used by the compiled shader must hold a
    // valid descriptor. They are written eagerly at set-creation time so the descriptor set is valid
    // from the first frame regardless of when — or whether — a mask is assigned.
    class TerrainSurfaceMaskTexture
    {
    public:
        explicit TerrainSurfaceMaskTexture(core::Device& device);
        ~TerrainSurfaceMaskTexture();

        TerrainSurfaceMaskTexture(const TerrainSurfaceMaskTexture&) = delete;
        TerrainSurfaceMaskTexture& operator=(const TerrainSurfaceMaskTexture&) = delete;

        // Idempotent: creates the 1x1 dummy image, its sampler and the params UBO. Safe to call every
        // frame; does work once.
        void ensureResources();

        // (Re)creates the mask image at the given size. A same-size request keeps the existing image
        // so a repeated assign does not churn device memory. Returns false on failure, in which case
        // the object stays in its previous (possibly dummy-only) state.
        bool createMask(uint32_t width, uint32_t height);

        // Queues a full-image RGBA8 upload. The bytes are copied, not referenced: the caller's CPU
        // buffer is the paintable master and keeps being mutated.
        void queueUpload(const std::vector<uint8_t>& rgba);

        void setParams(float minX, float minZ, float maxX, float maxZ,
                       float wetnessScale, float snowScale, bool enabled);

        // Records the pending copy. MUST be called outside any render pass — same contract as
        // PluginTextureManager::flushUploads, and it is called from the same place for that reason.
        void flushUploads(const vk::CommandBuffer& commandBuffer);

        void releaseMask();
        void cleanup();

        // Always valid once ensureResources() has run: falls back to the 1x1 dummy when no mask is
        // assigned, so the descriptor never dangles.
        [[nodiscard]] vk::ImageView getImageView() const { return maskView ? maskView : dummyView; }
        [[nodiscard]] vk::Sampler getSampler() const { return sampler; }
        [[nodiscard]] vk::Buffer getParamsBuffer() const { return paramsBuffer; }
        [[nodiscard]] vk::DeviceSize getParamsSize() const { return sizeof(TerrainWeatherMaskUBOData); }

        [[nodiscard]] bool hasMask() const { return maskImage != vk::Image{}; }
        [[nodiscard]] uint32_t getWidth() const { return width; }
        [[nodiscard]] uint32_t getHeight() const { return height; }

        // True once between a resource change and the descriptor rewrite that must follow it.
        [[nodiscard]] bool consumeDescriptorDirty();

    private:
        void writeParams();

        core::Device& device;

        vk::Image maskImage{};
        core::VulkanAllocation maskAllocation{};
        vk::ImageView maskView{};
        uint32_t width = 0;
        uint32_t height = 0;

        vk::Image dummyImage{};
        core::VulkanAllocation dummyAllocation{};
        vk::ImageView dummyView{};

        vk::Sampler sampler{};

        vk::Buffer paramsBuffer{};
        core::VulkanAllocation paramsAllocation{};
        TerrainWeatherMaskUBOData params{};

        core::PerFrameBuffer staging;
        std::vector<uint8_t> pendingData;
        bool uploadPending = false;
        bool descriptorDirty = false;
        bool resourcesCreated = false;
    };
}
