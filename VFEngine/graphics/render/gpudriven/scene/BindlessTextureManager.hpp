#pragma once

#include "../GPUDrivenTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <string>
#include <mutex>
#include <unordered_map>

namespace core {
    class Device;
}

namespace render::gpudriven {

    class TextureStreamManager;

    class BindlessTextureManager {
        friend class TextureStreamManager;

    private:
        core::Device& device;
        
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;
        
        mutable std::mutex textureMutex;
        std::unordered_map<std::string, uint32_t> texturePathToIndex;
        uint32_t nextTextureIndex = 1; // Index 0 is reserved for default texture
        std::vector<uint32_t> freeIndices;

        uint32_t requestedMaxTextures = MAX_BINDLESS_TEXTURES;
        uint32_t effectiveMaxTextures = MAX_BINDLESS_TEXTURES;
        bool initialized = false;
        bool defaultTextureSet = false;
        vk::ImageView defaultImageView;
        vk::Sampler defaultSampler;

    public:
        explicit BindlessTextureManager(core::Device& device, uint32_t maxTextures = MAX_BINDLESS_TEXTURES);
        ~BindlessTextureManager();

        BindlessTextureManager(const BindlessTextureManager&) = delete;
        BindlessTextureManager& operator=(const BindlessTextureManager&) = delete;

        void init();

        void cleanup();

        uint32_t registerTexture(const std::string& path, vk::ImageView imageView, vk::Sampler sampler);

        // Re-point an already-registered slot at a new view/sampler (e.g. per-frame RTT views).
        // Requires UpdateAfterBind (set on this manager) and that the slot is not referenced by
        // an in-flight submission at the time of the write.
        void updateTexture(uint32_t index, vk::ImageView imageView, vk::Sampler sampler);

        void unregisterTexture(const std::string& path);

        uint32_t getTextureIndex(const std::string& path) const;

        void setDefaultTexture(vk::ImageView imageView, vk::Sampler sampler);

        vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }

        vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }

        uint32_t getRegisteredTextureCount() const { return nextTextureIndex - 1 - static_cast<uint32_t>(freeIndices.size()); }

        vk::ImageView getDefaultImageView() const { return defaultImageView; }
        vk::Sampler getDefaultSampler() const { return defaultSampler; }
        bool hasDefaultTexture() const { return defaultTextureSet; }

    private:
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void updateDescriptor(uint32_t index, vk::ImageView imageView, vk::Sampler sampler);
    };

}
