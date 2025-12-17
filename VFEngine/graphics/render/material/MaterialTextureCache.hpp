#pragma once
#include <vulkan/vulkan.hpp>
#include <string>
#include <unordered_map>

namespace core
{
    class Device;
}

namespace render::mesh
{
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
    private:
        core::Device& device;
        vk::CommandPool commandPool;

        struct TextureGPU {
            vk::Image image;
            vk::DeviceMemory memory;
            vk::ImageView view;
            vk::Sampler sampler;
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
        static constexpr int MAX_MATERIAL_DESCRIPTOR_SETS = 256;

        // Cache of material path -> descriptor set
        std::unordered_map<std::string, vk::DescriptorSet> materialDescriptorSets;

    public:
        static constexpr int MAX_MATERIAL_TEXTURES = 6;

        explicit MaterialTextureCache(core::Device& device);
        ~MaterialTextureCache();

        // Non-copyable
        MaterialTextureCache(const MaterialTextureCache&) = delete;
        MaterialTextureCache& operator=(const MaterialTextureCache&) = delete;
        
        void init(vk::CommandPool commandPool);
        void initDescriptorResources(vk::DescriptorSetLayout layout);
        
        void resetDescriptorResources();
        
        void cleanUp();
        
        bool loadTexture(const std::string& path);
        
        vk::DescriptorSet getOrCreateMaterialDescriptorSet(
            const std::string& materialPath,
            const MaterialTexturePaths& textures);
        
        void invalidateMaterialDescriptorSet(const std::string& materialPath);

        // Get default 1x1 white texture view/sampler
        vk::ImageView getDefaultView() const { return defaultTexture.view; }
        vk::Sampler getDefaultSampler() const { return defaultTexture.sampler; }
        
        bool hasDefaultTexture() const { return defaultTextureCreated; }
        bool hasDescriptorResources() const { return descriptorPoolCreated; }

        // Get texture view/sampler by path (for building descriptor sets)
        vk::ImageView getViewForPath(const std::string& path) const;
        vk::Sampler getSamplerForPath(const std::string& path) const;

    private:
        void createDefaultTexture();
        void createDescriptorPool();
        vk::DescriptorSet allocateDescriptorSet();
        void updateMaterialDescriptorSet(vk::DescriptorSet set, const MaterialTexturePaths& textures);
    };
}
