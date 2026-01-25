#pragma once

#include "ClusterGridTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>

namespace core
{
    class Device;
}

namespace render::lighting
{
    class ClusterGridManager
    {
    private:
        core::Device& device;

        // Configuration
        ClusterGridConfig config;
        ClusterCameraParams cachedCameraParams{};

        // Params UBO (HOST_VISIBLE for direct updates)
        vk::Buffer paramsBuffer;
        vk::DeviceMemory paramsMemory;
        void* paramsMapped = nullptr;

        // Cluster AABBs SSBO (DEVICE_LOCAL, updated via staging)
        vk::Buffer clusterAABBBuffer;
        vk::DeviceMemory clusterAABBMemory;
        vk::Buffer clusterAABBStagingBuffer;
        vk::DeviceMemory clusterAABBStagingMemory;
        void* clusterAABBStagingMapped = nullptr;

        // Descriptor resources
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        // CPU-side cluster data
        std::vector<GPUClusterAABB> cpuClusterAABBs;
        GPUClusterGridParams cpuParams{};

        // State tracking
        bool initialized = false;
        bool needsRebuild = true;
        bool needsUpload = false;

    public:
        explicit ClusterGridManager(core::Device& device);
        ~ClusterGridManager();

        ClusterGridManager(const ClusterGridManager&) = delete;
        ClusterGridManager& operator=(const ClusterGridManager&) = delete;

        void init(const ClusterGridConfig& config = {});
        void cleanup();

        // Update cluster grid when camera parameters change
        void updateFromCamera(const ClusterCameraParams& cameraParams);

        // Upload to GPU (call within command buffer recording)
        void uploadToGPU(vk::CommandBuffer cmd);

        [[nodiscard]] vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        [[nodiscard]] vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }
        [[nodiscard]] vk::Buffer getParamsBuffer() const { return paramsBuffer; }
        [[nodiscard]] vk::Buffer getClusterAABBBuffer() const { return clusterAABBBuffer; }
        [[nodiscard]] const ClusterGridConfig& getConfig() const { return config; }
        [[nodiscard]] uint32_t getTotalClusters() const { return config.getTotalClusters(); }
        [[nodiscard]] bool isInitialized() const { return initialized; }
        [[nodiscard]] const std::vector<GPUClusterAABB>& getClusterAABBs() const { return cpuClusterAABBs; }

        [[nodiscard]] std::vector<uint32_t> getClusterIndicesForPointLight(
            const glm::vec3& lightPosViewSpace,
            float radius) const;

        [[nodiscard]] std::vector<uint32_t> getClusterIndicesForSpotLight(
            const glm::vec3& lightPosViewSpace,
            const glm::vec3& lightDirViewSpace,
            float range,
            float outerAngleCos) const;

    private:
        void createBuffers();
        void destroyBuffers();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void updateDescriptors();

        // Core cluster computation
        void rebuildClusterGrid();
        void computeClusterAABBs();
        void updateParamsBuffer();

        // Dirty detection
        [[nodiscard]] bool detectCameraChanges(const ClusterCameraParams& newParams) const;

        // Helper: unproject NDC point to view-space at given depth
        [[nodiscard]] glm::vec3 unprojectToViewSpace(float ndcX, float ndcY, float viewZ) const;
    };
}
