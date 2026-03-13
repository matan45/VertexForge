#pragma once

#include <vulkan/vulkan.hpp>
#include <cstdint>

namespace render::water
{
    struct OceanFFTConfig
    {
        uint32_t resolution = 256;
        float patchSize = 100.0f;
        float windSpeed = 8.0f;
        float windDirection = 45.0f;
        float amplitude = 0.00003f;
        float choppiness = 1.2f;
        float gravity = 9.81f;
        float foamThreshold = -0.1f;
        float displacementScale = 4.0f;
    };

    struct SpectrumPushConstants
    {
        uint32_t N;
        float patchSize;
        float windSpeed;
        float windDirX;
        float windDirZ;
        float amplitude;
        float gravity;
        float cutoffLow;
        uint32_t seed;
        uint32_t padding;
    };
    static_assert(sizeof(SpectrumPushConstants) == 40);

    struct TimeEvolvePushConstants
    {
        uint32_t N;
        float time;
        float choppiness;
        float patchSize;
        float gravity;
        uint32_t padding1;
        uint32_t padding2;
        uint32_t padding3;
    };
    static_assert(sizeof(TimeEvolvePushConstants) == 32);

    struct FFTPushConstants
    {
        uint32_t N;
        uint32_t stage;
        uint32_t direction;
        uint32_t padding;
    };
    static_assert(sizeof(FFTPushConstants) == 16);

    struct MergePushConstants
    {
        uint32_t N;
        float choppiness;
        float patchSize;
        float foamThreshold;
        float displacementScale;
    };
    static_assert(sizeof(MergePushConstants) == 20);

    struct FieldPair
    {
        vk::Image images[2];
        vk::DeviceMemory memory[2];
        vk::ImageView views[2];
    };

    static constexpr uint32_t WORKGROUP_SIZE = 16;
}
