#pragma once
#include "RenderSettings.hpp"

namespace types
{
    // VK-1655: whether the scene's shadow settings render shadows at all. Quality Off means off, exactly
    // like enabled=false (the editor's Quality combo offers "Off"; before VK-1655 it kept shadows on).
    inline bool shadowsRendered(const ShadowSettings& settings)
    {
        return settings.enabled && settings.quality != ShadowQuality::Off;
    }
}
