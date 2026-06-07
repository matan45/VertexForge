#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <cstddef>
#include <unordered_map>
#include <vector>
#include "../../core/VulkanMemoryManager.hpp"
#include "../../core/PerFrameBuffer.hpp"
#include "../../../services/data/PluginTextureTypes.hpp"

namespace core
{
    class Device;
}

namespace render::custom
{
    // Engine-side owner of plugin-created 2D textures (per-frame CPU upload) and the
    // single world-space mask binding. Mirrors CustomPipelineManager: plugins only see
    // opaque handles — every Vulkan object lives here so no Vulkan call crosses the
    // plugin DLL boundary.
    //
    // One texture at a time may be bound as a world-space XZ-projected mask consumed
    // by the terrain (set 11 binding 3/4) and entity (WORLD_MASK_SET binding 0/1)
    // mesh-shader pipelines. The first bind lazily creates the mask descriptor layout
    // and requests a one-time pipeline recreate (WORLD_MASK_ENABLED macro); unbind and
    // runtime toggles only rewrite the params UBO — no pipeline recreate.
    class PluginTextureManager
    {
    public:
        // std140 mirror of the WorldMaskUBO block in the shaders.
        struct WorldMaskUBOData
        {
            glm::vec4 worldMinMax{0.0f};      // minX, minZ, maxX, maxZ
            float terrainDimMin = 1.0f;
            float entityDiscardBelow = 0.0f;
            uint32_t flags = 0;               // bit0 enabled, bit1 affectsTerrain, bit2 affectsEntities
            float pad = 0.0f;
        };

        static constexpr uint32_t MASK_FLAG_ENABLED          = 1u << 0;
        static constexpr uint32_t MASK_FLAG_AFFECTS_TERRAIN  = 1u << 1;
        static constexpr uint32_t MASK_FLAG_AFFECTS_ENTITIES = 1u << 2;

        static uint32_t packMaskFlags(const plugin::WorldMaskParams& params, bool debugForceDisabled)
        {
            uint32_t flags = 0;
            if (params.enabled && !debugForceDisabled) flags |= MASK_FLAG_ENABLED;
            if (params.affectsTerrain) flags |= MASK_FLAG_AFFECTS_TERRAIN;
            if (params.affectsEntities) flags |= MASK_FLAG_AFFECTS_ENTITIES;
            return flags;
        }

    private:
        struct TextureEntry
        {
            uint32_t width = 0;
            uint32_t height = 0;
            plugin::TextureFormat format = plugin::TextureFormat::R8;
            vk::Image image;
            core::VulkanAllocation imageAllocation;
            vk::ImageView view;
            vk::Sampler sampler;
            core::PerFrameBuffer staging;       // MAX_FRAMES_IN_FLIGHT host-visible slots
            std::vector<std::byte> pendingData; // latest CPU bytes, copied in flushUploads
            bool dirty = false;
        };

        core::Device& device;

        std::unordered_map<uint64_t, TextureEntry> textures;
        uint64_t nextId = 1;

        // World mask state (single mask).
        uint64_t boundMaskId = 0;
        plugin::WorldMaskParams maskParams;
        WorldMaskUBOData maskUBOData;
        bool debugForceDisabled = false;

        vk::Buffer maskParamsBuffer;            // persistent host-coherent UBO
        core::VulkanAllocation maskParamsAllocation;

        vk::Image dummyImage;                   // 1x1 white R8 — keeps descriptors valid when unbound
        core::VulkanAllocation dummyAllocation;
        vk::ImageView dummyView;
        vk::Sampler dummySampler;

        vk::DescriptorSetLayout entityMaskLayout;   // created lazily on first bind
        vk::DescriptorPool entityMaskPool;
        vk::DescriptorSet entityMaskDescriptorSet;
        bool maskResourcesCreated = false;
        bool needsPipelineRecreate = false;
        uint64_t maskDescriptorVersion = 0;     // bumped on bind/unbind — terrain rewrites b3/b4 on change

    public:
        explicit PluginTextureManager(core::Device& device);
        ~PluginTextureManager();

        PluginTextureManager(const PluginTextureManager&) = delete;
        PluginTextureManager& operator=(const PluginTextureManager&) = delete;

        plugin::PluginTextureHandle createTexture2D(uint32_t width, uint32_t height,
                                                    plugin::TextureFormat format);
        void updateTexture2D(plugin::PluginTextureHandle handle, std::vector<std::byte>&& data);
        void destroyTexture2D(plugin::PluginTextureHandle handle);

        void bindWorldMask(plugin::PluginTextureHandle handle,
                           const glm::vec3& worldMin, const glm::vec3& worldMax,
                           const plugin::WorldMaskParams& params);
        void unbindWorldMask();
        void setWorldMaskParams(const plugin::WorldMaskParams& params);

        // Editor/debug override layered over the plugin's enabled flag — UBO write only.
        void setDebugMaskEnabled(bool enabled);
        bool getDebugMaskEnabled() const { return !debugForceDisabled; }

        // CPU readback of the bound mask's red channel at a world (x,z) position,
        // nearest-cell sampled from the retained pendingData. Returns 1.0 when no
        // mask is bound, the mask is disabled, or the point is outside the bounds —
        // the same "no effect" cases the shaders treat as mask = 1.0.
        [[nodiscard]] float sampleWorldMask(float worldX, float worldZ) const;

        // Records staging->image copies for dirty textures. Must be called outside any
        // render pass, before the scene pass is recorded (same command buffer).
        void flushUploads(const vk::CommandBuffer& commandBuffer);

        // True exactly once after the first bindWorldMask — the caller recreates the
        // terrain + entity pipelines with WORLD_MASK_ENABLED.
        bool consumeNeedsPipelineRecreate();

        [[nodiscard]] bool hasMaskResources() const { return maskResourcesCreated; }
        [[nodiscard]] uint64_t getMaskDescriptorVersion() const { return maskDescriptorVersion; }
        [[nodiscard]] vk::DescriptorSetLayout getEntityMaskLayout() const { return entityMaskLayout; }
        [[nodiscard]] vk::DescriptorSet getEntityMaskDescriptorSet() const { return entityMaskDescriptorSet; }

        // Resources for the terrain pipeline's own set-11 descriptor writes (b3/b4).
        [[nodiscard]] vk::ImageView getMaskImageView() const;   // bound mask view or dummy
        [[nodiscard]] vk::Sampler getMaskSampler() const;
        [[nodiscard]] vk::Buffer getMaskParamsBuffer() const { return maskParamsBuffer; }
        [[nodiscard]] static constexpr vk::DeviceSize getMaskParamsSize() { return sizeof(WorldMaskUBOData); }

        void cleanUp();

    private:
        void ensureMaskResources();
        void writeMaskParamsUBO();
        void updateEntityMaskDescriptor();
        void destroyTextureEntry(TextureEntry& entry);

        static vk::Format toVkFormat(plugin::TextureFormat format);
    };
}
