#pragma once

#include "ShadowTypes.hpp"
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

        struct CubeEntry
        {
            std::unique_ptr<ShadowCubeMap> resource;
            bool allocated = false;
        };

        std::vector<CubeEntry> cubeMaps;
        std::vector<uint32_t> freeCubeIndices;

        vk::Sampler cubeComparisonSampler;
        vk::Sampler cubeDepthSampler;

        std::unique_ptr<ShadowCubeMap> placeholderCube;

        bool initialized = false;

    public:
        explicit ShadowResourcePool(core::Device& device);
        ~ShadowResourcePool();

        ShadowResourcePool(const ShadowResourcePool&) = delete;
        ShadowResourcePool& operator=(const ShadowResourcePool&) = delete;

        void init();
        void cleanup();

        [[nodiscard]] ShadowResourceHandle allocateCube(uint32_t size);
        void free(const ShadowResourceHandle& handle);
        void setDeletionQueue(core::DeferredDeletionQueue* queue);
        void freeAll();

        [[nodiscard]] ShadowCubeMap* getCube(const ShadowResourceHandle& handle);
        [[nodiscard]] const ShadowCubeMap* getCube(const ShadowResourceHandle& handle) const;

        [[nodiscard]] vk::Sampler getCubeComparisonSampler() const { return cubeComparisonSampler; }
        [[nodiscard]] vk::Sampler getCubeDepthSampler() const { return cubeDepthSampler; }
        [[nodiscard]] vk::ImageView getPlaceholderCubeView() const;

        [[nodiscard]] uint32_t getActiveCubeCount() const;
        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void createSamplers();
        void createPlaceholderResources();
        void cleanupSamplers();
        void cleanupPlaceholders();
    };
}
