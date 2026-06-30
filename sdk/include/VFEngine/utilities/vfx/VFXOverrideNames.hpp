#pragma once

#include <array>
#include <string>
#include <string_view>
#include <algorithm>

namespace vfx::overridenames
{
    inline constexpr std::string_view spawnRate = "spawnRate";
    inline constexpr std::string_view lifetime = "lifetime";
    inline constexpr std::string_view startSize = "startSize";
    inline constexpr std::string_view startSpeed = "startSpeed";
    inline constexpr std::string_view stretchMultiplier = "stretchMultiplier";
    inline constexpr std::string_view windStrength = "windStrength";
    inline constexpr std::string_view gravityStrength = "gravityStrength";
    inline constexpr std::string_view softParticleDistance = "softParticleDistance";
    inline constexpr std::string_view lightingInfluence = "lightingInfluence";
    inline constexpr std::string_view collisionLifetimeLoss = "collisionLifetimeLoss";
    inline constexpr std::string_view coneSpread = "coneSpread";
    inline constexpr std::string_view renderMode = "renderMode";
    inline constexpr std::string_view collisionEnabled = "collisionEnabled";

    inline constexpr std::string_view emitDirection = "emitDirection";
    inline constexpr std::string_view windDirection = "windDirection";
    inline constexpr std::string_view gravityDirection = "gravityDirection";
    inline constexpr std::string_view shapeDimensions = "shapeDimensions";
    inline constexpr std::string_view startColor = "startColor";

    inline constexpr std::array scalarOverrides{
        spawnRate,
        lifetime,
        startSize,
        startSpeed,
        stretchMultiplier,
        windStrength,
        gravityStrength,
        softParticleDistance,
        lightingInfluence,
        collisionLifetimeLoss,
        coneSpread,
        renderMode,
        collisionEnabled
    };

    inline constexpr std::array vectorOverrides{
        emitDirection,
        windDirection,
        gravityDirection,
        shapeDimensions,
        startColor
    };

    // Accepts std::string (implicit) and string literals; a separate
    // const std::string& overload would make literal calls ambiguous.
    inline bool isScalarOverride(std::string_view name)
    {
        return std::find(scalarOverrides.begin(), scalarOverrides.end(), name) != scalarOverrides.end();
    }

    inline bool isVectorOverride(std::string_view name)
    {
        return std::find(vectorOverrides.begin(), vectorOverrides.end(), name) != vectorOverrides.end();
    }
}
