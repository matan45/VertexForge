#include "OceanFFTReadback.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"

#include <cmath>

namespace render::water
{
    OceanFFTReadback::OceanFFTReadback(core::Device& device)
        : device(device)
    {
    }

    OceanFFTReadback::~OceanFFTReadback()
    {
        cleanup();
    }

    void OceanFFTReadback::init(uint32_t resolution)
    {
        // RGBA16F = 8 bytes per pixel
        vk::DeviceSize bufferSize = resolution * resolution * 8;

        for (uint32_t i = 0; i < core::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            core::BufferInfoRequest req(
                device.getLogicalDevice(),
                device.getPhysicalDevice(),
                bufferSize,
                vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

            core::BufferUtilities::createBuffer(req, readbackBuffers[i], readbackMemories[i]);

            readbackMapped[i] = device.getLogicalDevice().mapMemory(readbackMemories[i], 0, bufferSize);
        }

        readbackFrameIndex = 0;
        readbackFrameCount = 0;
    }

    void OceanFFTReadback::cleanup()
    {
        vk::Device vkDevice = device.getLogicalDevice();
        for (uint32_t i = 0; i < core::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            if (readbackMapped[i])
            {
                vkDevice.unmapMemory(readbackMemories[i]);
                readbackMapped[i] = nullptr;
            }
            if (readbackBuffers[i])
            {
                core::BufferUtilities::destroyBuffer(vkDevice, readbackBuffers[i], readbackMemories[i]);
                readbackBuffers[i] = nullptr;
                readbackMemories[i] = nullptr;
            }
        }
        cpuDisplacementData.clear();
        readbackFrameIndex = 0;
        readbackFrameCount = 0;
    }

    void OceanFFTReadback::recordCopy(vk::CommandBuffer cmd, vk::Image displacementImage, uint32_t resolution)
    {
        vk::Buffer targetBuffer = readbackBuffers[readbackFrameIndex];

        // Barrier: compute write -> transfer read
        vk::ImageMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        barrier.oldLayout = vk::ImageLayout::eGeneral;
        barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.image = displacementImage;
        barrier.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eTransfer,
            {}, {}, {}, barrier);

        // Copy image to ring buffer slot
        vk::BufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = vk::Offset3D{0, 0, 0};
        region.imageExtent = vk::Extent3D{resolution, resolution, 1};

        cmd.copyImageToBuffer(displacementImage, vk::ImageLayout::eTransferSrcOptimal,
                              targetBuffer, region);

        // Barrier: transfer -> back to general for next frame's compute
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.newLayout = vk::ImageLayout::eGeneral;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, {}, barrier);

        ++readbackFrameCount;
        readbackFrameIndex = (readbackFrameIndex + 1) % core::MAX_FRAMES_IN_FLIGHT;
    }

    void OceanFFTReadback::readback(uint32_t resolution)
    {
        if (readbackFrameCount < core::MAX_FRAMES_IN_FLIGHT)
            return;

        void* mapped = readbackMapped[readbackFrameIndex];
        if (!mapped)
            return;

        uint32_t N = resolution;
        const uint16_t* halfData = static_cast<const uint16_t*>(mapped);
        cpuDisplacementData.resize(N * N);

        for (uint32_t i = 0; i < N * N; ++i)
        {
            auto halfToFloat = [](uint16_t h) -> float
            {
                uint32_t sign = (h >> 15) & 0x1;
                uint32_t exp = (h >> 10) & 0x1F;
                uint32_t mant = h & 0x3FF;

                if (exp == 0)
                {
                    if (mant == 0) return sign ? -0.0f : 0.0f;
                    float f = std::ldexp(static_cast<float>(mant), -24);
                    return sign ? -f : f;
                }
                if (exp == 31)
                {
                    if (mant == 0) return sign ? -INFINITY : INFINITY;
                    return NAN;
                }

                float f = std::ldexp(static_cast<float>(mant | 0x400), static_cast<int>(exp) - 25);
                float result = sign ? -f : f;
                return std::isnan(result) ? 0.0f : result;
            };

            cpuDisplacementData[i] = glm::vec4(
                halfToFloat(halfData[i * 4 + 0]),
                halfToFloat(halfData[i * 4 + 1]),
                halfToFloat(halfData[i * 4 + 2]),
                halfToFloat(halfData[i * 4 + 3])
            );
        }
    }

    float OceanFFTReadback::sampleHeightAt(const glm::vec2& worldXZ, uint32_t resolution, float patchSize) const
    {
        if (cpuDisplacementData.empty() || patchSize <= 0.0f)
            return 0.0f;

        uint32_t N = resolution;

        float u = worldXZ.x / patchSize;
        float v = worldXZ.y / patchSize;

        u = u - std::floor(u);
        v = v - std::floor(v);

        float fx = u * N - 0.5f;
        float fy = v * N - 0.5f;

        int x0 = static_cast<int>(std::floor(fx));
        int y0 = static_cast<int>(std::floor(fy));
        float fracX = fx - x0;
        float fracY = fy - y0;

        auto wrap = [N](int c) -> uint32_t { return static_cast<uint32_t>(((c % static_cast<int>(N)) + N) % N); };
        uint32_t x0w = wrap(x0), x1w = wrap(x0 + 1);
        uint32_t y0w = wrap(y0), y1w = wrap(y0 + 1);

        float h00 = cpuDisplacementData[y0w * N + x0w].y;
        float h10 = cpuDisplacementData[y0w * N + x1w].y;
        float h01 = cpuDisplacementData[y1w * N + x0w].y;
        float h11 = cpuDisplacementData[y1w * N + x1w].y;

        float h0 = h00 + fracX * (h10 - h00);
        float h1 = h01 + fracX * (h11 - h01);

        return h0 + fracY * (h1 - h0);
    }
}
