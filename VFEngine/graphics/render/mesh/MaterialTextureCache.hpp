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
    // Texture paths for a material (used for per-material descriptor sets)
    struct MaterialTexturePaths
    {
        std::string albedo;
        std::string metallic;
        std::string roughness;
        std::string ao;
        std::string normal;
        std::string emission;
    };

    class MaterialTextureCache
    {
    public:
        // Max textures per material (matches shader u_Textures[6])
        static constexpr int MAX_MATERIAL_TEXTURES = 6;

        explicit MaterialTextureCache(core::Device& device);
        ~MaterialTextureCache();

        // Non-copyable
        MaterialTextureCache(const MaterialTextureCache&) = delete;
        MaterialTextureCache& operator=(const MaterialTextureCache&) = delete;

        // Initialize with command pool for GPU uploads
        void init(vk::CommandPool commandPool);

        // Initialize descriptor resources (call after pipeline creates layout)
        void initDescriptorResources(vk::DescriptorSetLayout layout);

        // Cleanup all GPU resources
        void cleanUp();

        // Load a texture from file path, returns true if successful
        bool loadTexture(const std::string& path);

        // Get or create a per-material descriptor set
        // Returns the descriptor set for this material, or nullptr if failed
        vk::DescriptorSet getOrCreateMaterialDescriptorSet(
            const std::string& materialPath,
            const MaterialTexturePaths& textures);

        // Invalidate a material's descriptor set (call when material changes)
        void invalidateMaterialDescriptorSet(const std::string& materialPath);

        // Get default 1x1 white texture view/sampler
        vk::ImageView getDefaultView() const { return defaultTexture.view; }
        vk::Sampler getDefaultSampler() const { return defaultTexture.sampler; }

        // Check if default texture is created
        bool hasDefaultTexture() const { return defaultTextureCreated; }

        // Check if descriptor resources are initialized
        bool hasDescriptorResources() const { return descriptorPoolCreated; }

        // Get texture view/sampler by path (for building descriptor sets)
        vk::ImageView getViewForPath(const std::string& path) const;
        vk::Sampler getSamplerForPath(const std::string& path) const;

        // Legacy methods for backward compatibility (global texture array)
        int getTextureSlot(const std::string& path);
        void resetSlotAssignments();
        vk::ImageView getViewForSlot(int slot) const;
        vk::Sampler getSamplerForSlot(int slot) const;
        std::array<vk::ImageView, 16> getImageViews() const;
        std::array<vk::Sampler, 16> getSamplers() const;

    private:
        core::Device& device;
        vk::CommandPool commandPool;

        struct TextureGPU {
            vk::Image image;
            vk::DeviceMemory memory;
            vk::ImageView view;
            vk::Sampler sampler;
            int slotIndex = -1;  // Legacy: slot in global array
        };

        // Texture cache (path -> GPU texture)
        std::unordered_map<std::string, TextureGPU> textureCache;

        // Default 1x1 white texture for empty slots
        TextureGPU defaultTexture{};
        bool defaultTextureCreated = false;

        // Per-material descriptor sets
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        bool descriptorPoolCreated = false;
        static constexpr int MAX_MATERIAL_DESCRIPTOR_SETS = 256;  // Max materials

        // Cache of material path -> descriptor set
        std::unordered_map<std::string, vk::DescriptorSet> materialDescriptorSets;

        // Legacy: Currently bound textures (slot index -> path), max 16
        std::array<std::string, 16> boundTexturePaths;
        int nextTextureSlot = 0;

        void createDefaultTexture();
        void createDescriptorPool();
        vk::DescriptorSet allocateDescriptorSet();
        void updateMaterialDescriptorSet(vk::DescriptorSet set, const MaterialTexturePaths& textures);
    };
}
