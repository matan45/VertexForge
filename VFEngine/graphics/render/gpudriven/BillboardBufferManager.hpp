#pragma once

#include "BillboardGPUTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <cstdint>

namespace core
{
    class Device;
}

namespace render::gpudriven
{
    class BillboardBufferManager
    {
    public:
        void init(core::Device& device);
        void cleanup();

        void uploadInstances(const std::vector<BillboardInstanceGPU>& instances);
        void clear();

        // Reset count buffer on GPU timeline (call during command buffer recording)
        void resetCountBuffer(vk::CommandBuffer cmd);

        [[nodiscard]] vk::Buffer getInstanceBuffer() const { return instanceBuffer; }
        [[nodiscard]] vk::Buffer getCountBuffer() const { return countBuffer; }
        [[nodiscard]] uint32_t getInstanceCount() const { return currentInstanceCount; }
        [[nodiscard]] uint32_t getCapacity() const { return capacity; }
        [[nodiscard]] bool needsCountReset() const { return pendingCountReset; }

    private:
        void createBuffers(uint32_t maxInstances);
        void destroyBuffers();

        core::Device* devicePtr = nullptr;

        vk::Buffer instanceBuffer;
        vk::DeviceMemory instanceBufferMemory;
        vk::Buffer countBuffer;
        vk::DeviceMemory countBufferMemory;

        uint32_t capacity = 0;
        uint32_t currentInstanceCount = 0;
        bool pendingCountReset = false;
    };
}
