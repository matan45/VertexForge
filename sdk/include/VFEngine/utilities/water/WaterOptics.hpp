#pragma once

#include <glm/glm.hpp>
#include <cmath>

namespace water
{
    // VK-1604: CPU mirror of the Beer-Lambert absorption / in-scattering math in
    // resources/shaders/water/water.glsl. Keep both in sync — these exist so the optics are
    // unit-testable without a GPU, and so the "flag off == unchanged" guarantee rests on
    // properties that a test can actually assert (transmittance(0) == 1, monotone decay, ...).
    //
    // Coefficients are in 1/metre, matching the per-channel absorption already used by
    // resources/shaders/postprocess/underwater.glsl. Note that shader applies an ad-hoc 3x
    // distance multiplier; it is deliberately NOT replicated here (see the water.glsl comment) —
    // above- and below-waterline absorption therefore still differ slightly. Tracked separately.

    // Fraction of light surviving `pathLength` metres of water, per channel.
    inline glm::vec3 transmittance(const glm::vec3& absorptionCoeff, float pathLength)
    {
        const float d = glm::max(pathLength, 0.0f);
        return glm::vec3(std::exp(-absorptionCoeff.x * d),
                         std::exp(-absorptionCoeff.y * d),
                         std::exp(-absorptionCoeff.z * d));
    }

    // Light scattered into the view along the same path. Saturates to scatterColor.
    inline glm::vec3 inScatter(const glm::vec3& scatterColor, const glm::vec3& scatterCoeff,
                               float pathLength)
    {
        const float d = glm::max(pathLength, 0.0f);
        return scatterColor * (glm::vec3(1.0f) - glm::vec3(std::exp(-scatterCoeff.x * d),
                                                            std::exp(-scatterCoeff.y * d),
                                                            std::exp(-scatterCoeff.z * d)));
    }

    // Length of the refracted path: down to the sea floor and back up to the viewer. Plain
    // vertical depth under-attenuates badly at grazing angles, which is where water is most
    // often seen, so the return trip is folded in via NdotV.
    inline float refractedPathLength(float verticalDepth, float NdotV, float maxDistance)
    {
        const float d = glm::max(verticalDepth, 0.0f);
        const float path = d * (1.0f + 1.0f / glm::max(NdotV, 0.1f)) * 0.5f;
        return glm::clamp(path, 0.0f, maxDistance);
    }

    // Applies absorption then in-scattering to the refracted scene colour.
    inline glm::vec3 applyWaterOptics(const glm::vec3& refractedColor,
                                      const glm::vec3& absorptionCoeff,
                                      const glm::vec3& scatterColor,
                                      const glm::vec3& scatterCoeff,
                                      float pathLength)
    {
        return refractedColor * transmittance(absorptionCoeff, pathLength)
             + inScatter(scatterColor, scatterCoeff, pathLength);
    }

    // CPU twin of linearizeDepth() in resources/shaders/common/cluster_culling.glsl. The engine
    // uses a standard (non-reverse) [0,1] depth range — PipelineUtilities sets eLessOrEqual.
    inline float linearizeDepth(float nearPlane, float farPlane, float windowZ)
    {
        const float denominator = glm::max(farPlane - windowZ * (farPlane - nearPlane), 0.0001f);
        return nearPlane * farPlane / denominator;
    }
}
