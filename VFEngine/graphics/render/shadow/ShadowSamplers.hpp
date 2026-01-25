#pragma once

#include <vulkan/vulkan.hpp>

namespace render::shadow
{
    /**
     * Utility class for creating shadow map samplers.
     * Provides centralized sampler creation for different shadow texture types.
     */
    class ShadowSamplers
    {
    public:
        /**
         * Creates a standard sampler for reading shadow map depth values directly.
         * Uses linear filtering and clamp-to-border addressing.
         * Border color is white (1.0) representing maximum depth (no shadow).
         */
        static vk::Sampler createStandardSampler(vk::Device device);

        /**
         * Creates a comparison sampler for hardware-accelerated PCF.
         * Uses depth comparison (LessOrEqual) for shadow testing.
         * Suitable for 2D textures and texture arrays (CSM).
         */
        static vk::Sampler createComparisonSampler(vk::Device device);

        /**
         * Creates a comparison sampler optimized for cube maps.
         * Same as comparison sampler but configured for cube map addressing.
         * Used for point light omnidirectional shadows.
         */
        static vk::Sampler createCubeComparisonSampler(vk::Device device);

        /**
         * Creates a nearest-neighbor sampler for debug visualization.
         * Allows reading exact depth values without filtering.
         */
        static vk::Sampler createDebugSampler(vk::Device device);

    private:
        static vk::SamplerCreateInfo createBaseSamplerInfo();
    };
}
