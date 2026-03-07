#pragma once
#include <vulkan/vulkan.hpp>
#include <array>
#include <memory>
#include <string>
#include <unordered_map>

namespace core
{
    class Device;
    class Texture;
}

namespace render::mesh
{
    inline constexpr uint32_t DEFAULT_TEXTURE_SIZE = 1;
    inline constexpr std::array<uint8_t, 4> DEFAULT_TEXTURE_PIXELS = {255, 255, 255, 255}; // RGBA white
    struct MaterialTexturePaths
    {
        std::string albedo;
        std::string normal; 
        std::string orm;
        
        std::string metallic;
        std::string roughness;
        std::string ao;
        
        std::string emission;
        std::string height;
        
        // Additional texture slots for material graph use (mixing, animation, etc.)
        std::string texture8; 
        std::string texture9;
        std::string texture10; 
        std::string texture11; 
        std::string texture12; 
        std::string texture13; 
        std::string texture14;
        std::string texture15;
        
        bool usesORM() const { return !orm.empty(); }
        
        const std::string& getPath(int slotIndex) const
        {
            switch (slotIndex)
            {
            case 0: return albedo;
            case 1: return normal;
            case 2: return orm;
            case 3: return metallic;
            case 4: return roughness;
            case 5: return ao;
            case 6: return emission;
            case 7: return height;
            case 8: return texture8;
            case 9: return texture9;
            case 10: return texture10;
            case 11: return texture11;
            case 12: return texture12;
            case 13: return texture13;
            case 14: return texture14;
            case 15: return texture15;
            default: return albedo; // Fallback
            }
        }
    };

    class MaterialTextureCache
    {
    private:
        core::Device& device;
        vk::CommandPool commandPool;

        // Default texture (1x1 white) - managed separately for simplicity
        struct DefaultTexture
        {
            vk::Image image;
            vk::DeviceMemory memory;
            vk::ImageView view;
            vk::Sampler sampler;
        };

        // Texture cache (path -> Texture object)
        std::unordered_map<std::string, std::unique_ptr<core::Texture>> textureCache;

        DefaultTexture defaultTexture{};
        bool defaultTextureCreated = false;
        
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        bool descriptorPoolCreated = false;
        static constexpr int MAX_MATERIAL_DESCRIPTOR_SETS = 256;
        
        std::unordered_map<std::string, vk::DescriptorSet> materialDescriptorSets;

    public:
        explicit MaterialTextureCache(core::Device& device);
        ~MaterialTextureCache();

        // Non-copyable, non-movable (due to unique_ptr with incomplete type in header)
        MaterialTextureCache(const MaterialTextureCache&) = delete;
        MaterialTextureCache& operator=(const MaterialTextureCache&) = delete;
        MaterialTextureCache(MaterialTextureCache&&) = delete;
        MaterialTextureCache& operator=(MaterialTextureCache&&) = delete;

        void init(vk::CommandPool commandPool);
        void initDescriptorResources(vk::DescriptorSetLayout layout);

        void resetDescriptorResources();

        void cleanUp();

        bool loadTexture(const std::string& path);

        vk::DescriptorSet getOrCreateMaterialDescriptorSet(
            const std::string& materialPath,
            const MaterialTexturePaths& textures);

        void invalidateMaterialDescriptorSet(const std::string& materialPath);
        
        vk::ImageView getDefaultView() const { return defaultTexture.view; }
        vk::Sampler getDefaultSampler() const { return defaultTexture.sampler; }

        bool hasDefaultTexture() const { return defaultTextureCreated; }
        bool hasDescriptorResources() const { return descriptorPoolCreated; }
        
        void unloadTexture(const std::string& path);

        vk::ImageView getViewForPath(const std::string& path) const;
        vk::Sampler getSamplerForPath(const std::string& path) const;

    private:
        void createDefaultTexture();
        void createDescriptorPool();
        vk::DescriptorSet allocateDescriptorSet();
        void updateMaterialDescriptorSet(vk::DescriptorSet set, const MaterialTexturePaths& textures);
    };
}
