#pragma once

#include "VFXParameterRegistry.hpp"
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

    namespace detail
    {
        constexpr size_t countScalarOverrides()
        {
            size_t count = 0;
            for (const auto& parameter : kExposedParameters)
            {
                if (isScalarOverrideType(parameter.type))
                    ++count;
            }
            return count;
        }

        constexpr size_t countVectorOverrides()
        {
            size_t count = 0;
            for (const auto& parameter : kExposedParameters)
            {
                if (isVectorOverrideType(parameter.type))
                    ++count;
            }
            return count;
        }

        template <size_t Count>
        constexpr std::array<std::string_view, Count> makeOverrideArray(bool scalar)
        {
            std::array<std::string_view, Count> names{};
            size_t index = 0;
            for (const auto& parameter : kExposedParameters)
            {
                const bool include = scalar
                    ? isScalarOverrideType(parameter.type)
                    : isVectorOverrideType(parameter.type);
                if (include)
                    names[index++] = parameter.name;
            }
            return names;
        }
    }

    inline constexpr std::array scalarOverrides =
        detail::makeOverrideArray<detail::countScalarOverrides()>(true);

    inline constexpr std::array vectorOverrides =
        detail::makeOverrideArray<detail::countVectorOverrides()>(false);

    // Accepts std::string (implicit) and string literals; a separate
    // const std::string& overload would make literal calls ambiguous.
    inline bool isScalarOverride(std::string_view name)
    {
        const VFXExposedParameter* parameter = findExposedParameter(name);
        return parameter && isScalarOverrideType(parameter->type);
    }

    inline bool isVectorOverride(std::string_view name)
    {
        const VFXExposedParameter* parameter = findExposedParameter(name);
        return parameter && isVectorOverrideType(parameter->type);
    }
}
