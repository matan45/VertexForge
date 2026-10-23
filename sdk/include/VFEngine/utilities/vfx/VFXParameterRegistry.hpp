#pragma once

#include "VFXTypes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
        StartColor,
        // VK-1657: per-instance render state (re-pushed to the pipelines' per-slot config) and a
        // colour multiplier applied over start colour + colour-over-lifetime, unlike startColor.
        BlendMode,
        SortOrder,
        ColorTint
    };

    // VK-1657 value ranges of the Int overrides (blendMode is a VFXBlendMode value; sortOrder
    // matches the asset loader's clamp in VFXEmitterConfigLoader).
    inline constexpr int32_t kBlendModeCount = 4;
    inline constexpr int32_t kMinSortOrder = -256;
    inline constexpr int32_t kMaxSortOrder = 256;

    struct VFXExposedParameter
    {
        std::string_view name;
        VFXPropertyType type = VFXPropertyType::Float;
        VFXOverrideTarget target = VFXOverrideTarget::SpawnRate;
        std::string_view label;
    };

    inline constexpr std::array<VFXExposedParameter, 21> kExposedParameters{{
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
        {"startColor", VFXPropertyType::Color, VFXOverrideTarget::StartColor, "Start Color"},
        {"blendMode", VFXPropertyType::Int, VFXOverrideTarget::BlendMode, "Blend Mode"},
        {"sortOrder", VFXPropertyType::Int, VFXOverrideTarget::SortOrder, "Sort Order"},
        {"colorTint", VFXPropertyType::Color, VFXOverrideTarget::ColorTint, "Color Tint"}
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

    // VK-1657: a value an override of `parameter` may store — the registry type, finite numbers,
    // blendMode a VFXBlendMode value, sortOrder inside the loader clamp, colorTint non-negative.
    inline bool isValidOverrideValue(const VFXExposedParameter& parameter, const VFXPropertyValue& value)
    {
        if (!valueMatchesType(value, parameter.type))
            return false;

        if (const auto* number = std::get_if<float>(&value))
            return std::isfinite(*number);
        if (const auto* vector = std::get_if<glm::vec3>(&value))
            return std::isfinite(vector->x) && std::isfinite(vector->y) && std::isfinite(vector->z);
        if (const auto* color = std::get_if<glm::vec4>(&value))
        {
            if (!std::isfinite(color->x) || !std::isfinite(color->y) ||
                !std::isfinite(color->z) || !std::isfinite(color->w))
                return false;
            if (parameter.target == VFXOverrideTarget::ColorTint)
                return color->x >= 0.0f && color->y >= 0.0f && color->z >= 0.0f && color->w >= 0.0f;
            return true;
        }
        if (const auto* integer = std::get_if<int32_t>(&value))
        {
            if (parameter.target == VFXOverrideTarget::BlendMode)
                return *integer >= 0 && *integer < kBlendModeCount;
            if (parameter.target == VFXOverrideTarget::SortOrder)
                return *integer >= kMinSortOrder && *integer <= kMaxSortOrder;
            return true;
        }
        return true;
    }

    inline bool isValidOverride(const VFXParamOverride& overrideValue)
    {
        const VFXExposedParameter* parameter = findExposedParameter(overrideValue.name);
        return parameter && isValidOverrideValue(*parameter, overrideValue.value);
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
