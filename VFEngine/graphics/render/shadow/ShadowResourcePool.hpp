#pragma once

#include "ShadowTypes.hpp"
#include "ShadowDepthArray.hpp"
#include "ShadowCubeMap.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <cstdint>

namespace core
{
    class Device;
}

namespace render::shadow
{
    /**
     * Manages dedicated shadow texture resources (texture arrays and cube maps).
     *
     * Responsibilities:
     * - Allocates and frees ShadowDepthArray (for CSM) and ShadowCubeMap (for point lights)
     * - Manages shared comparison samplers
     * - Provides descriptor set layouts and descriptor sets for shader binding
     *
     * Note: Spot light shadows use ShadowAtlasManager instead (atlas-based allocation).
     */
    class ShadowResourcePool
    {
    public:
        explicit ShadowResourcePool(core::Device& device);
        ~ShadowResourcePool();

        ShadowResourcePool(const ShadowResourcePool&) = delete;
        ShadowResourcePool& operator=(const ShadowResourcePool&) = delete;

        void init();
        void cleanup();
        void recreate();

        // ========================================
        // Resource Allocation
        // ========================================

        /**
         * Allocate a depth texture array for CSM.
         * @param width Width of each cascade layer
         * @param height Height of each cascade layer
         * @param layers Number of cascade layers (typically 1-4)
         * @return Handle to the allocated resource
         */
        [[nodiscard]] ShadowResourceHandle allocateArray(uint32_t width, uint32_t height, uint32_t layers);

        /**
         * Allocate a cube depth texture for point light shadows.
         * @param size Width and height of each cube face
         * @return Handle to the allocated resource
         */
        [[nodiscard]] ShadowResourceHandle allocateCube(uint32_t size);

        /**
         * Free a previously allocated resource.
         * @param handle Handle returned from allocateArray or allocateCube
         */
        void free(const ShadowResourceHandle& handle);

        /**
         * Free all allocated resources.
         */
        void freeAll();

        // ========================================
        // Resource Access
        // ========================================

        /**
         * Get the ShadowDepthArray for a given handle.
         * @param handle Handle with resourceType == Array
         * @return Pointer to the array resource, or nullptr if invalid
         */
        [[nodiscard]] ShadowDepthArray* getArray(const ShadowResourceHandle& handle);
        [[nodiscard]] const ShadowDepthArray* getArray(const ShadowResourceHandle& handle) const;

        /**
         * Get the ShadowCubeMap for a given handle.
         * @param handle Handle with resourceType == Cube
         * @return Pointer to the cube resource, or nullptr if invalid
         */
        [[nodiscard]] ShadowCubeMap* getCube(const ShadowResourceHandle& handle);
        [[nodiscard]] const ShadowCubeMap* getCube(const ShadowResourceHandle& handle) const;

        // ========================================
        // Sampler Access
        // ========================================

        /**
         * Get comparison sampler for 2D/array shadow textures.
         * Uses depth comparison for hardware PCF.
         */
        [[nodiscard]] vk::Sampler getComparisonSampler() const { return comparisonSampler; }

        /**
         * Get comparison sampler for cube shadow textures.
         */
        [[nodiscard]] vk::Sampler getCubeComparisonSampler() const { return cubeComparisonSampler; }

        /**
         * Get standard (non-comparison) sampler for debug visualization.
         */
        [[nodiscard]] vk::Sampler getStandardSampler() const { return standardSampler; }

        // ========================================
        // Descriptor Access
        // ========================================

        /**
         * Get descriptor set layout for binding depth arrays to shaders.
         * Binding 0: sampler2DArrayShadow (CSM cascades)
         */
        [[nodiscard]] vk::DescriptorSetLayout getArrayDescriptorLayout() const { return arrayDescriptorLayout; }

        /**
         * Get descriptor set layout for binding cube maps to shaders.
         * Binding 0: samplerCubeShadow (point light shadows)
         */
        [[nodiscard]] vk::DescriptorSetLayout getCubeDescriptorLayout() const { return cubeDescriptorLayout; }

        // ========================================
        // Statistics
        // ========================================

        [[nodiscard]] uint32_t getActiveArrayCount() const;
        [[nodiscard]] uint32_t getActiveCubeCount() const;
        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        core::Device& device;

        // Resource pools
        struct ArrayEntry
        {
            std::unique_ptr<ShadowDepthArray> resource;
            bool allocated = false;
        };
        std::vector<ArrayEntry> depthArrays;
        uint32_t nextArrayIndex = 0;

        struct CubeEntry
        {
            std::unique_ptr<ShadowCubeMap> resource;
            bool allocated = false;
        };
        std::vector<CubeEntry> cubeMaps;
        uint32_t nextCubeIndex = 0;

        // Shared samplers
        vk::Sampler standardSampler;
        vk::Sampler comparisonSampler;
        vk::Sampler cubeComparisonSampler;

        // Descriptor resources
        vk::DescriptorSetLayout arrayDescriptorLayout;
        vk::DescriptorSetLayout cubeDescriptorLayout;
        vk::DescriptorPool descriptorPool;

        bool initialized = false;

        void createSamplers();
        void createDescriptorLayouts();
        void cleanupDescriptors();
        void cleanupSamplers();
    };
}
