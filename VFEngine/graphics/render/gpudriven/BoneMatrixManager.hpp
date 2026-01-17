#pragma once

#include "GPUDrivenTypes.hpp"
#include "FreeListAllocator.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <unordered_map>
#include <vector>

namespace core {
    class Device;
}

namespace render::gpudriven {

    // Information about a single animated object's bone allocation
    struct AnimatedObjectBoneData {
        uint32_t boneMatrixOffset;  // Offset into global bone buffer
        uint32_t boneCount;         // Number of active bones
        bool dirty = false;         // Needs upload this frame
    };

    class BoneMatrixManager {
    private:
        core::Device& device;

        // GPU buffers for bone matrices
        vk::Buffer boneBuffer;
        vk::DeviceMemory boneBufferMemory;
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;
        void* stagingMapped = nullptr;

        // Descriptor resources
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        // Allocator for bone palette offsets
        FreeListAllocator boneAllocator;

        // Entity to bone data mapping
        std::unordered_map<entt::entity, AnimatedObjectBoneData> allocations;

        // CPU-side bone matrix storage (for staging)
        std::vector<glm::mat4> cpuBoneMatrices;

        // Dirty tracking for efficient uploads
        std::vector<entt::entity> dirtyEntities;

        uint32_t maxBoneMatrices = 0;
        bool initialized = false;

    public:
        explicit BoneMatrixManager(core::Device& device);
        ~BoneMatrixManager();

        // Non-copyable
        BoneMatrixManager(const BoneMatrixManager&) = delete;
        BoneMatrixManager& operator=(const BoneMatrixManager&) = delete;

        void init();
        void cleanup();

        // Allocate space for an animated object's bone palette
        // Returns bone offset or INVALID_BONE_OFFSET on failure
        uint32_t allocate(entt::entity entity, uint32_t boneCount);

        // Free bone allocation for an entity
        void free(entt::entity entity);

        // Check if entity has bone allocation
        bool hasAllocation(entt::entity entity) const;

        // Get bone offset for entity (INVALID_BONE_OFFSET if not allocated)
        uint32_t getBoneOffset(entt::entity entity) const;

        // Update bone matrices for an entity
        void updateBoneMatrices(entt::entity entity, const std::vector<glm::mat4>& matrices);

        // Upload all dirty bone matrices to GPU
        void uploadToGPU(vk::CommandBuffer cmd);

        // Accessors for rendering
        vk::Buffer getBoneBuffer() const { return boneBuffer; }
        vk::DeviceSize getBoneBufferSize() const { return maxBoneMatrices * sizeof(glm::mat4); }
        vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }

        // Statistics
        uint32_t getAllocatedObjectCount() const { return static_cast<uint32_t>(allocations.size()); }
        uint32_t getTotalAllocatedBones() const { return boneAllocator.getUsedCount(); }

    private:
        void createBuffers();
        void destroyBuffers();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void updateDescriptor();
    };

}
