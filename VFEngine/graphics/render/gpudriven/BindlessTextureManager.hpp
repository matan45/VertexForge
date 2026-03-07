#pragma once

#include "GPUDrivenTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <string>
#include <unordered_map>

namespace core {
    class Device;
}

namespace render::gpudriven {
    
    class BindlessTextureManager {
    private:
        core::Device& device;
        
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;
        
        std::unordered_map<std::string, uint32_t> texturePathToIndex;
        uint32_t nextTextureIndex = 1; // Index 0 is reserved for default texture
        std::vector<uint32_t> freeIndices;

        bool initialized = false;
        bool defaultTextureSet = false;

    public:
        explicit BindlessTextureManager(core::Device& device);
        ~BindlessTextureManager();

        // Non-copyable
        BindlessTextureManager(const BindlessTextureManager&) = delete;
        BindlessTextureManager& operator=(const BindlessTextureManager&) = delete;

        void init();

        void cleanup();

        uint32_t registerTexture(const std::string& path, vk::ImageView imageView, vk::Sampler sampler);

        void unregisterTexture(const std::string& path);

        uint32_t getTextureIndex(const std::string& path) const;

        void setDefaultTexture(vk::ImageView imageView, vk::Sampler sampler);

        vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }

        vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }

        uint32_t getRegisteredTextureCount() const { return nextTextureIndex - 1 - static_cast<uint32_t>(freeIndices.size()); }

    private:
        // Helper methods
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void updateDescriptor(uint32_t index, vk::ImageView imageView, vk::Sampler sampler);
    };

}
