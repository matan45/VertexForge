#pragma once

#include "VFXTypes.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace vfx
{
    enum class VFXOverrideTarget : uint8_t
    {
        SpawnRate,
        Lifetime,
        StartSize,
        StartSpeed,
        StretchMultiplier,
        WindStrength,
        GravityStrength,
        SoftParticleDistance,
        LightingInfluence,
        CollisionLifetimeLoss,
        ConeSpread,
        RenderMode,
        CollisionEnabled,
        EmitDirection,
        WindDirection,
        GravityDirection,
        ShapeDimensions,
        StartColor
    };

    struct VFXExposedParameter
    {
        std::string_view name;
        VFXPropertyType type = VFXPropertyType::Float;
        VFXOverrideTarget target = VFXOverrideTarget::SpawnRate;
        std::string_view label;
    };

    inline constexpr std::array<VFXExposedParameter, 18> kExposedParameters{{
        {"spawnRate", VFXPropertyType::Float, VFXOverrideTarget::SpawnRate, "Spawn Rate"},
        {"lifetime", VFXPropertyType::Float, VFXOverrideTarget::Lifetime, "Lifetime"},
        {"startSize", VFXPropertyType::Float, VFXOverrideTarget::StartSize, "Start Size"},
        {"startSpeed", VFXPropertyType::Float, VFXOverrideTarget::StartSpeed, "Start Speed"},
        {"stretchMultiplier", VFXPropertyType::Float, VFXOverrideTarget::StretchMultiplier, "Stretch Multiplier"},
        {"windStrength", VFXPropertyType::Float, VFXOverrideTarget::WindStrength, "Wind Strength"},
        {"gravityStrength", VFXPropertyType::Float, VFXOverrideTarget::GravityStrength, "Gravity Strength"},
        {"softParticleDistance", VFXPropertyType::Float, VFXOverrideTarget::SoftParticleDistance, "Soft Particle Distance"},
        {"lightingInfluence", VFXPropertyType::Float, VFXOverrideTarget::LightingInfluence, "Lighting Influence"},
        {"collisionLifetimeLoss", VFXPropertyType::Float, VFXOverrideTarget::CollisionLifetimeLoss, "Collision Lifetime Loss"},
        {"coneSpread", VFXPropertyType::Float, VFXOverrideTarget::ConeSpread, "Cone Spread"},
        {"renderMode", VFXPropertyType::Int, VFXOverrideTarget::RenderMode, "Render Mode"},
        {"collisionEnabled", VFXPropertyType::Bool, VFXOverrideTarget::CollisionEnabled, "Collision Enabled"},
        {"emitDirection", VFXPropertyType::Vec3, VFXOverrideTarget::EmitDirection, "Emit Direction"},
        {"windDirection", VFXPropertyType::Vec3, VFXOverrideTarget::WindDirection, "Wind Direction"},
        {"gravityDirection", VFXPropertyType::Vec3, VFXOverrideTarget::GravityDirection, "Gravity Direction"},
        {"shapeDimensions", VFXPropertyType::Vec3, VFXOverrideTarget::ShapeDimensions, "Shape Dimensions"},
        {"startColor", VFXPropertyType::Color, VFXOverrideTarget::StartColor, "Start Color"}
    }};

    struct VFXParamOverride
    {
        std::string name;
        VFXPropertyValue value;
    };

    inline constexpr bool isScalarOverrideType(VFXPropertyType type)
    {
        return type == VFXPropertyType::Float ||
               type == VFXPropertyType::Int ||
               type == VFXPropertyType::Bool;
    }

    inline constexpr bool isVectorOverrideType(VFXPropertyType type)
    {
        return type == VFXPropertyType::Vec3 ||
               type == VFXPropertyType::Color;
    }

    inline const VFXExposedParameter* findExposedParameter(std::string_view name)
    {
        const auto it = std::find_if(kExposedParameters.begin(), kExposedParameters.end(),
            [name](const VFXExposedParameter& parameter)
            {
                return parameter.name == name;
            });
        return it != kExposedParameters.end() ? &(*it) : nullptr;
    }

    inline bool isExposedParameter(std::string_view name)
    {
        return findExposedParameter(name) != nullptr;
    }

    inline bool valueMatchesType(const VFXPropertyValue& value, VFXPropertyType type)
    {
        switch (type)
        {
        case VFXPropertyType::Float:    return std::holds_alternative<float>(value);
        case VFXPropertyType::Vec2:     return std::holds_alternative<glm::vec2>(value);
        case VFXPropertyType::Vec3:     return std::holds_alternative<glm::vec3>(value);
        case VFXPropertyType::Vec4:
        case VFXPropertyType::Color:    return std::holds_alternative<glm::vec4>(value);
        case VFXPropertyType::Int:      return std::holds_alternative<int32_t>(value);
        case VFXPropertyType::Bool:     return std::holds_alternative<bool>(value);
        case VFXPropertyType::String:   return std::holds_alternative<std::string>(value);
        case VFXPropertyType::Curve:    return std::holds_alternative<VFXCurve>(value);
        case VFXPropertyType::Gradient: return std::holds_alternative<VFXGradient>(value);
        default:                        return false;
        }
    }

    inline VFXPropertyValue defaultValueFor(VFXPropertyType type)
    {
        switch (type)
        {
        case VFXPropertyType::Float:    return 0.0f;
        case VFXPropertyType::Vec2:     return glm::vec2(0.0f);
        case VFXPropertyType::Vec3:     return glm::vec3(0.0f);
        case VFXPropertyType::Vec4:     return glm::vec4(0.0f);
        case VFXPropertyType::Color:    return glm::vec4(1.0f);
        case VFXPropertyType::Int:      return int32_t{0};
        case VFXPropertyType::Bool:     return false;
        case VFXPropertyType::String:   return std::string{};
        case VFXPropertyType::Curve:    return VFXCurve{};
        case VFXPropertyType::Gradient: return VFXGradient{};
        default:                        return 0.0f;
        }
    }
}
