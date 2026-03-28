#pragma once

#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <cstdint>
#include "../../core/RenderManager.hpp"

namespace core
{
    class Device;
}

namespace render::water
{
    class OceanFFTReadback
    {
    public:
        explicit OceanFFTReadback(core::Device& device);
        ~OceanFFTReadback();

        OceanFFTReadback(const OceanFFTReadback&) = delete;
        OceanFFTReadback& operator=(const OceanFFTReadback&) = delete;

        void init(uint32_t resolution);
        void cleanup();

        void recordCopy(vk::CommandBuffer cmd, vk::Image displacementImage, uint32_t resolution);
        void readback(uint32_t resolution);
        [[nodiscard]] float sampleHeightAt(const glm::vec2& worldXZ, uint32_t resolution, float patchSize) const;

    private:
        core::Device& device;

        std::array<vk::Buffer, core::MAX_FRAMES_IN_FLIGHT> readbackBuffers{};
        std::array<core::VulkanAllocation, core::MAX_FRAMES_IN_FLIGHT> readbackAllocations{};
        std::array<void*, core::MAX_FRAMES_IN_FLIGHT> readbackMapped{};
        std::vector<glm::vec4> cpuDisplacementData;
        uint32_t readbackFrameIndex = 0;
        uint32_t readbackFrameCount = 0;
    };
}
