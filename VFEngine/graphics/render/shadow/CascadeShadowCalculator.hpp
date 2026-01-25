#pragma once

#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <cstdint>
#include "types/RenderSettings.hpp"

namespace render::shadow
{
    /**
     * Data structure containing computed cascade shadow map parameters.
     */
    struct CascadeData
    {
        float nearDistance = 0.0f;      // Distance from camera to cascade near plane
        float farDistance = 0.0f;       // Distance from camera to cascade far plane
        glm::mat4 viewMatrix{1.0f};     // Light view matrix
        glm::mat4 projMatrix{1.0f};     // Orthographic projection matrix
        glm::mat4 viewProjMatrix{1.0f}; // Combined view-projection matrix
        float texelSize = 0.0f;         // World units per texel for stable snapping
    };

    /**
     * CascadeShadowCalculator - Utility class for computing cascade shadow map matrices.
     *
     * Provides static methods for:
     * - Computing cascade split distances (Linear, Logarithmic, Practical modes)
     * - Extracting camera frustum corners in world space
     * - Computing stable orthographic projection matrices that prevent shadow swimming
     *
     * The key to stable shadows is texel-grid snapping, which ensures the shadow map
     * doesn't shift when the camera moves, preventing shadow "swimming" artifacts.
     */
    class CascadeShadowCalculator
    {
    public:
        /**
         * Compute split distances for cascade shadow maps.
         *
         * @param cameraNear Camera near plane distance
         * @param cameraFar Camera far plane distance
         * @param cascadeCount Number of cascades (1-4)
         * @param splitMode Split distribution mode
         * @param lambda Blend factor for Practical mode (0=linear, 1=logarithmic)
         * @return Vector of split distances (size = cascadeCount + 1, includes near and far)
         */
        static std::vector<float> computeSplitDistances(
            float cameraNear,
            float cameraFar,
            uint32_t cascadeCount,
            types::CascadeSplitMode splitMode,
            float lambda = 0.75f);

        /**
         * Extract frustum corners in world space for a given depth range.
         *
         * Uses Vulkan NDC (z=0 near, z=1 far) and interpolates the full frustum
         * corners to the specified near/far sub-range.
         *
         * @param cameraView Camera view matrix
         * @param cameraProjection Camera projection matrix
         * @param nearDist Near distance for the sub-frustum
         * @param farDist Far distance for the sub-frustum
         * @return 8 corners: [0-3] near plane (BL, BR, TR, TL), [4-7] far plane
         */
        static std::array<glm::vec3, 8> getFrustumCornersWorldSpace(
            const glm::mat4& cameraView,
            const glm::mat4& cameraProjection,
            float nearDist,
            float farDist);

        /**
         * Compute stable light view-projection matrix for a cascade.
         *
         * Key stability features:
         * - Uses bounding sphere for rotation-invariant bounds
         * - Snaps light-space bounds to texel grid (prevents shadow swimming)
         * - Extends Z range to catch shadow casters behind the camera frustum
         * - Applies Vulkan Y-flip to projection matrix
         *
         * @param frustumCorners 8 corners of the cascade sub-frustum in world space
         * @param lightDirection Normalized world-space light direction
         * @param shadowMapResolution Resolution of the shadow map (for texel snapping)
         * @return CascadeData with computed matrices and parameters
         */
        static CascadeData computeCascadeMatrix(
            const std::array<glm::vec3, 8>& frustumCorners,
            const glm::vec3& lightDirection,
            uint32_t shadowMapResolution);

    private:
        /**
         * Snap a value to the nearest texel boundary.
         * This is the key to preventing shadow swimming.
         */
        static float snapToTexel(float value, float texelSize);
    };
}
