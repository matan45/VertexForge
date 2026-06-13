#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace rendertexture
{
    using RenderTextureId = uint32_t;
    constexpr RenderTextureId INVALID_RENDER_TEXTURE_ID = 0;

    enum class UpdateMode : uint8_t
    {
        EveryFrame,
        OnDemand,
        FixedInterval
    };

    struct RenderTextureDesc
    {
        uint32_t width = 512;
        uint32_t height = 512;
        UpdateMode updateMode = UpdateMode::EveryFrame;
        float fixedIntervalSeconds = 1.0f / 30.0f;
        glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
        uint32_t priority = 0;
        bool renderShadows = false; // false = flat-lit (e.g. minimap); true = sample shadows
    };
}
