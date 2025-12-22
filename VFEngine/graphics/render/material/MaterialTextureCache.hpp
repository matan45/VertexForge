#pragma once
#include <vulkan/vulkan.hpp>
#include <string>
#include <unordered_map>
#include "../../../utilities/material/MaterialTypes.hpp"

namespace core
{
    class Device;
}

namespace render::mesh
{
    // Texture paths for all material texture slots
    // Supports both ORM packed workflow and legacy individual textures
    struct MaterialTexturePaths
    {
        // Core PBR textures
        std::string albedo;         // Slot 0: RGB color, A for opacity
        std::string normal;         // Slot 1: Tangent-space normal map
        std::string orm;            // Slot 2: Packed ORM (R=AO, G=Roughness, B=Metallic)

        // Legacy individual textures (for backward compatibility)
        std::string metallic;       // Slot 3: Individual metallic map
        std::string roughness;      // Slot 4: Individual roughness map
        std::string ao;             // Slot 5: Individual ambient occlusion map

        // Additional textures
        std::string emission;       // Slot 6: RGB emission color
        std::string height;         // Slot 7: Height/displacement map
        std::string detailNormal;   // Slot 8: Secondary normal map
        std::string detailAlbedo;   // Slot 9: Secondary albedo
        std::string subsurface;     // Slot 10: Subsurface scattering
        std::string anisotropy;     // Slot 11: Anisotropic direction/strength
        std::string clearcoat;      // Slot 12: Clearcoat layer
        std::string clearcoatNormal;// Slot 13: Clearcoat normal map
        std::string reserved1;      // Slot 14: Reserved
        std::string reserved2;      // Slot 15: Reserved

        // Check if using packed ORM texture (vs individual metallic/roughness/ao)
        bool usesORM() const { return !orm.empty(); }

        // Get path for a specific slot index
        const std::string& getPath(int slotIndex) const {
            switch (slotIndex) {
                case 0: return albedo;
                case 1: return normal;
                case 2: return orm;
                case 3: return metallic;
                case 4: return roughness;
                case 5: return ao;
                case 6: return emission;
                case 7: return height;
                case 8: return detailNormal;
                case 9: return detailAlbedo;
                case 10: return subsurface;
                case 11: return anisotropy;
                case 12: return clearcoat;
                case 13: return clearcoatNormal;
                case 14: return reserved1;
                case 15: return reserved2;
                default: return albedo; // Fallback
            }
        }
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
