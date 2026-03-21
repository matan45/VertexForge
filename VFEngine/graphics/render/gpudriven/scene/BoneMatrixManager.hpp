#pragma once

#include "../FreeListAllocator.hpp"
#include "../../../core/RenderManager.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <array>
#include <unordered_map>
#include <vector>

namespace core {
    class Device;
}

namespace render::gpudriven {

    struct AnimatedObjectBoneData {
        uint32_t boneMatrixOffset;
        uint32_t boneCount;
        bool dirty = false;
    };

    class BoneMatrixManager {
    private:
        core::Device& device;

        vk::Buffer boneBuffer;
        vk::DeviceMemory boneBufferMemory;

        struct StagingFrame
        {
            vk::Buffer buffer;
            vk::DeviceMemory memory;
            void* mapped = nullptr;
        };

        std::array<StagingFrame, core::MAX_FRAMES_IN_FLIGHT> stagingFrames{};
        uint32_t currentStagingFrame = 0;

        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        FreeListAllocator boneAllocator;
        std::unordered_map<entt::entity, AnimatedObjectBoneData> allocations;
        std::vector<glm::mat4> cpuBoneMatrices;
        std::vector<entt::entity> dirtyEntities;

        uint32_t maxBoneMatrices = 0;
        bool initialized = false;

    public:
        explicit BoneMatrixManager(core::Device& device);
        ~BoneMatrixManager();

        BoneMatrixManager(const BoneMatrixManager&) = delete;
        BoneMatrixManager& operator=(const BoneMatrixManager&) = delete;

        void init();
        void cleanup();

        uint32_t allocate(entt::entity entity, uint32_t boneCount);
        void free(entt::entity entity);
        bool hasAllocation(entt::entity entity) const;
        uint32_t getBoneOffset(entt::entity entity) const;
        void updateBoneMatrices(entt::entity entity, const std::vector<glm::mat4>& matrices);
        void uploadToGPU(vk::CommandBuffer cmd);

        vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }

        void advanceStagingFrame() { currentStagingFrame = (currentStagingFrame + 1) % core::MAX_FRAMES_IN_FLIGHT; }

        uint32_t getAllocatedCount() const { return static_cast<uint32_t>(allocations.size()); }
        uint32_t getCapacity() const { return maxBoneMatrices; }
        float getFragmentationPercent() const { return boneAllocator.getFragmentationPercent(); }
        uint32_t getUsedBoneCount() const { return boneAllocator.getUsedCount() + boneAllocator.getReservedCount(); }

    private:
        void createBuffers();
        void destroyBuffers();
        void initializeGPUBuffer();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void updateDescriptor();
    };

}
