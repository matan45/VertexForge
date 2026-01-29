#pragma once

#include "ShadowTypes.hpp"
#include "ShadowDepthArray.hpp"
#include "ShadowCubeMap.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class DeferredDeletionQueue;
}

namespace render::shadow
{
    class ShadowResourcePool
    {
    private:
        core::Device& device;
        core::DeferredDeletionQueue* deletionQueue = nullptr;

        struct ArrayEntry
        {
            std::unique_ptr<ShadowDepthArray> resource;
            bool allocated = false;
        };

        std::vector<ArrayEntry> depthArrays;
        std::vector<uint32_t> freeArrayIndices;

        struct CubeEntry
        {
            std::unique_ptr<ShadowCubeMap> resource;
            bool allocated = false;
        };

        std::vector<CubeEntry> cubeMaps;
        std::vector<uint32_t> freeCubeIndices;

        vk::Sampler comparisonSampler;
        vk::Sampler cubeComparisonSampler;

        std::unique_ptr<ShadowDepthArray> placeholderArray;
        std::unique_ptr<ShadowCubeMap> placeholderCube;

        bool initialized = false;

    public:
        explicit ShadowResourcePool(core::Device& device);
        ~ShadowResourcePool();

        ShadowResourcePool(const ShadowResourcePool&) = delete;
        ShadowResourcePool& operator=(const ShadowResourcePool&) = delete;

        void init();
        void cleanup();

        [[nodiscard]] ShadowResourceHandle allocateArray(uint32_t width, uint32_t height, uint32_t layers);
        [[nodiscard]] ShadowResourceHandle allocateCube(uint32_t size);
        void free(const ShadowResourceHandle& handle);
        void setDeletionQueue(core::DeferredDeletionQueue* queue);
        void freeAll();

        [[nodiscard]] ShadowDepthArray* getArray(const ShadowResourceHandle& handle);
        [[nodiscard]] const ShadowDepthArray* getArray(const ShadowResourceHandle& handle) const;
        [[nodiscard]] ShadowCubeMap* getCube(const ShadowResourceHandle& handle);
        [[nodiscard]] const ShadowCubeMap* getCube(const ShadowResourceHandle& handle) const;

        [[nodiscard]] vk::Sampler getComparisonSampler() const { return comparisonSampler; }
        [[nodiscard]] vk::Sampler getCubeComparisonSampler() const { return cubeComparisonSampler; }
        [[nodiscard]] vk::ImageView getPlaceholderArrayView() const;
        [[nodiscard]] vk::ImageView getPlaceholderCubeView() const;

        [[nodiscard]] uint32_t getActiveArrayCount() const;
        [[nodiscard]] uint32_t getActiveCubeCount() const;
        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void createSamplers();
        void createPlaceholderResources();
        void cleanupSamplers();
        void cleanupPlaceholders();
    };
}
