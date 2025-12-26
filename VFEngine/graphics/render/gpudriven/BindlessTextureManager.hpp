#pragma once

#include "GPUDrivenTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {
    class Device;
}

namespace render::gpudriven {

    /**
     * Manages a global bindless texture array for GPU-driven rendering.
     *
     * Uses VK_EXT_descriptor_indexing features to create a large, partially-bound
     * array of samplers that can be indexed dynamically in shaders.
     *
     * Texture index 0 is reserved for the default white texture (fallback).
     * INVALID_TEXTURE_INDEX (0xFFFFFFFF) indicates no texture assigned.
     */
    class BindlessTextureManager {
    public:
        explicit BindlessTextureManager(core::Device& device);
        ~BindlessTextureManager();

        // Non-copyable
        BindlessTextureManager(const BindlessTextureManager&) = delete;
        BindlessTextureManager& operator=(const BindlessTextureManager&) = delete;

        /**
         * Initialize the bindless texture system.
         * Creates descriptor set layout, pool, and allocates the descriptor set.
         * Must be called before registering any textures.
         */
        void init();

        /**
         * Cleanup all GPU resources.
         */
        void cleanup();

        /**
         * Register a texture for bindless access.
         *
         * @param path Unique identifier for the texture (typically file path)
         * @param imageView The texture's image view
         * @param sampler The texture's sampler
         * @return Bindless index to use in shaders, or existing index if already registered
         */
        uint32_t registerTexture(const std::string& path, vk::ImageView imageView, vk::Sampler sampler);

        /**
         * Get the bindless index for a previously registered texture.
         *
         * @param path Texture path/identifier
         * @return Bindless index, or INVALID_TEXTURE_INDEX if not registered
         */
        uint32_t getTextureIndex(const std::string& path) const;

        /**
         * Check if a texture is already registered.
         */
        bool isTextureRegistered(const std::string& path) const;

        /**
         * Set the default texture (used at index 0 for fallback).
         * Should be a 1x1 white texture.
         */
        void setDefaultTexture(vk::ImageView imageView, vk::Sampler sampler);

        /**
         * Get the descriptor set layout for binding in pipelines.
         */
        vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }

        /**
         * Get the descriptor set containing all registered textures.
         */
        vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }

        /**
         * Get the number of registered textures (excluding default).
         */
        uint32_t getRegisteredTextureCount() const { return nextTextureIndex - 1; }

        /**
         * Get the maximum number of bindless textures supported.
         */
        uint32_t getMaxTextures() const { return MAX_BINDLESS_TEXTURES; }

        /**
         * Check if the manager is initialized.
         */
        bool isInitialized() const { return initialized; }

    private:
        core::Device& device;

        // Descriptor resources
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        // Texture tracking
        std::unordered_map<std::string, uint32_t> texturePathToIndex;
        uint32_t nextTextureIndex = 1; // Index 0 is reserved for default texture

        bool initialized = false;
        bool defaultTextureSet = false;

        // Helper methods
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void updateDescriptor(uint32_t index, vk::ImageView imageView, vk::Sampler sampler);
    };

}
