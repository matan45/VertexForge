#pragma once

// VK-1481: one shared bindless texture table for all four VFX GPU render pipelines
// (billboard / mesh / ribbon / distortion). It wraps render::gpudriven::BindlessTextureManager
// — which dedups by key and allocates a stable slot index but does NOT refcount — with a
// (colorSpace, path)-keyed refcount + 3-frame deferred teardown (VFXBindlessRefTable), owns the
// loaded core::Texture objects, and hosts the white (slot 0) and neutral-normal defaults.
//
// Replaces the previous per-pipeline "one descriptor set per unique texture path" scheme
// (MAX_TEXTURE_SLOTS = 64). Pipelines now store a per-emitter bindless index and bind this one
// descriptor set once per pass.
//
// Threading: initDistortion runs on the render thread while createInstance runs on the update
// thread, so both may acquire/release/tick concurrently — every public mutator locks mutex_.
// (The underlying manager separately serialises its vkUpdateDescriptorSets under its own mutex.)

#include "../../../core/VulkanMemoryManager.hpp" // core::VulkanAllocation
#include "vfx/VFXBindlessRefTable.hpp"

#include <vulkan/vulkan.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace core
{
    class Device;
    class Texture;
    class DeferredDeletionQueue;
}

namespace render::gpudriven
{
    class BindlessTextureManager;
}

namespace render::vfx
{
    class VFXBindlessTextures
    {
    public:
        static constexpr uint32_t CAPACITY = 2048;

        explicit VFXBindlessTextures(core::Device& device, uint32_t maxTextures = CAPACITY);
        ~VFXBindlessTextures();

        VFXBindlessTextures(const VFXBindlessTextures&) = delete;
        VFXBindlessTextures& operator=(const VFXBindlessTextures&) = delete;

        void init();
        void cleanup();

        void setDeletionQueue(core::DeferredDeletionQueue* dq);

        // Acquire the bindless slot for a texture path. srgb=true loads eR8G8B8A8Srgb
        // (billboard/mesh/ribbon); srgb=false loads eR8G8B8A8Unorm (distortion) — the two are
        // kept as independent slots so a shared path is not collapsed into one format. Returns
        // the slot (>= 1) on success, or defaultWhiteIndex() == 0 on empty/missing/full/load-error.
        uint32_t acquire(const std::string& path, bool srgb);

        // Drop the reference taken by a matching acquire(path, srgb). Teardown is deferred.
        void release(const std::string& path, bool srgb);

        // Advance the deferred-teardown clock and drain any slots whose window has elapsed.
        void tick(uint32_t frameNumber);

        uint32_t defaultWhiteIndex() const { return 0u; }
        uint32_t neutralNormalIndex() const { return neutralIdx_; }

        vk::DescriptorSetLayout getDescriptorSetLayout() const;
        vk::DescriptorSet getDescriptorSet() const;

    private:
        static std::string makeKey(const std::string& path, bool srgb)
        {
            return (srgb ? "s:" : "l:") + path;
        }

        // 1x1 UNORM image with the given RGBA pixel; backs the white + neutral-normal defaults.
        void createSolidImage(const std::array<uint8_t, 4>& rgba,
                              vk::Image& outImage, core::VulkanAllocation& outAlloc, vk::ImageView& outView);
        void destroySolidImage(vk::Image& image, core::VulkanAllocation& alloc, vk::ImageView& view);

        core::Device& device_;
        uint32_t maxTextures_;

        std::unique_ptr<render::gpudriven::BindlessTextureManager> manager_;
        ::vfx::VFXBindlessRefTable table_; // global ::vfx:: — NOT render::vfx:: (name-lookup gotcha)
        std::unordered_map<std::string, std::unique_ptr<core::Texture>> textures_; // key -> loaded texture

        core::DeferredDeletionQueue* deletionQueue_ = nullptr;
        std::mutex mutex_;
        uint32_t currentFrame_ = 0;

        // Built-in default slots (own their images; never released through the ref table).
        vk::Sampler sampler_{};
        vk::Image whiteImage_{};
        core::VulkanAllocation whiteAlloc_{};
        vk::ImageView whiteView_{};
        vk::Image neutralImage_{};
        core::VulkanAllocation neutralAlloc_{};
        vk::ImageView neutralView_{};
        uint32_t neutralIdx_ = 0;

        bool initialized_ = false;
    };
}
