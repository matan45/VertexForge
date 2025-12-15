#pragma once

#include <vulkan/vulkan.hpp>
#include <memory>
#include <string>
#include <array>
#include <unordered_map>

namespace core
{
    class Device;
}

namespace render::mesh
{
    class MaterialTextureCache
    {
    public:
        explicit MaterialTextureCache(core::Device& device);
        ~MaterialTextureCache();

        // Non-copyable
        MaterialTextureCache(const MaterialTextureCache&) = delete;
        MaterialTextureCache& operator=(const MaterialTextureCache&) = delete;

        // Initialize with command pool for GPU uploads
        void init(vk::CommandPool commandPool);

        // Cleanup all GPU resources
        void cleanUp();

        // Load a texture from file path, returns true if successful
        bool loadTexture(const std::string& path);

        // Get texture slot assignment for a path (-1 if not loaded/assigned)
        int getTextureSlot(const std::string& path);

        // Reset slot assignments for a new frame (call before assigning slots)
        void resetSlotAssignments();

        // Get default 1x1 white texture view/sampler
        vk::ImageView getDefaultView() const { return defaultTexture.view; }
        vk::Sampler getDefaultSampler() const { return defaultTexture.sampler; }

        // Get texture view/sampler by slot index
        vk::ImageView getViewForSlot(int slot) const;
        vk::Sampler getSamplerForSlot(int slot) const;

        // Build arrays for descriptor set update
        std::array<vk::ImageView, 8> getImageViews() const;
        std::array<vk::Sampler, 8> getSamplers() const;

        // Check if default texture is created
        bool hasDefaultTexture() const { return defaultTextureCreated; }

    private:
        core::Device& device;
        vk::CommandPool commandPool;

        struct TextureGPU {
            vk::Image image;
            vk::DeviceMemory memory;
            vk::ImageView view;
            vk::Sampler sampler;
            int slotIndex = -1;  // Slot in u_Textures[8], -1 = not assigned
        };

        // Texture cache (path -> GPU texture)
        std::unordered_map<std::string, TextureGPU> textureCache;

        // Default 1x1 white texture for empty slots
        TextureGPU defaultTexture{};
        bool defaultTextureCreated = false;

        // Currently bound textures (slot index -> path), max 8
        std::array<std::string, 8> boundTexturePaths;
        int nextTextureSlot = 0;

        void createDefaultTexture();
    };
}
