#include "OceanFFTReadback.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../../utilities/water/DisplacementSampling.hpp"

#include <cmath>
#include <immintrin.h>

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

            core::BufferUtilities::createBuffer(req, readbackBuffers[i], readbackAllocations[i], device.getMemoryManager());

            readbackMapped[i] = readbackAllocations[i].mappedPtr;
        }

        readbackFrameIndex = 0;
        readbackFrameCount = 0;
    }

    void OceanFFTReadback::cleanup()
    {
        vk::Device vkDevice = device.getLogicalDevice();
        for (uint32_t i = 0; i < core::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            readbackMapped[i] = nullptr;
            if (readbackBuffers[i])
            {
                core::BufferUtilities::destroyBuffer(vkDevice, readbackBuffers[i], readbackAllocations[i], device.getMemoryManager());
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

        // Use F16C SIMD: _mm_cvtph_ps converts 4 half-floats to 4 floats in one instruction
        for (uint32_t i = 0; i < N * N; ++i)
        {
            __m128i half4 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(&halfData[i * 4]));
            __m128 float4 = _mm_cvtph_ps(half4);
            _mm_storeu_ps(&cpuDisplacementData[i].x, float4);
        }
    }

    float OceanFFTReadback::sampleHeightAt(const glm::vec2& worldXZ, uint32_t resolution, float patchSize) const
    {
        return sampleHeightAtUV(::water::patchUV(worldXZ, patchSize), resolution);
    }

    // VK-1604: UV overload so the hex-tiling height path can sample the same band at three
    // per-cell-offset UVs. Wrap + bilinear live in utilities/water/DisplacementSampling.hpp so
    // they are shared with the tests and cannot drift from the readback.
    float OceanFFTReadback::sampleHeightAtUV(const glm::vec2& uv, uint32_t resolution) const
    {
        if (cpuDisplacementData.empty() || resolution == 0)
            return 0.0f;
        if (cpuDisplacementData.size() < static_cast<size_t>(resolution) * resolution)
            return 0.0f;

        return ::water::sampleBilinearWrapped(
            [this](uint32_t index) { return cpuDisplacementData[index].y; },
            resolution, uv);
    }
}
