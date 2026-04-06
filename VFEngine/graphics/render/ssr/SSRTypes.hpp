#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace render::ssr
{
    struct SSRSettings
    {
        bool enabled = false;
        float maxDistance = 100.0f;
        float intensity = 1.0f;
        float roughnessThreshold = 0.6f;
        float edgeFadeStart = 0.8f;
        float temporalBlend = 0.1f;
        uint32_t maxSteps = 64;
        bool halfResolution = true;
    };

    struct alignas(16) SSRParamsUBO
    {
        glm::mat4 projection;
        glm::mat4 inverseProjection;
        glm::mat4 view;
        glm::mat4 inverseView;
        glm::mat4 prevViewProjection;
        glm::vec4 params;        // maxDistance, intensity, roughnessThreshold, edgeFadeStart
        glm::vec2 resolution;
        glm::vec2 texelSize;
        float nearPlane;
        float farPlane;
        uint32_t maxSteps;
        uint32_t frameIndex;
        uint32_t historyValid;
        uint32_t halfResolution;
        float temporalBlend;
        float padding;
    };
}
