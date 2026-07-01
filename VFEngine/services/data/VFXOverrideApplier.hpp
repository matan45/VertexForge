#pragma once

#include "VFXTypes.hpp"
#include <vfx/VFXParameterRegistry.hpp>

#include <glm/glm.hpp>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace services
{
    inline bool applyOverride(VFXEmitterOverrides& out, std::string_view name, const vfx::VFXPropertyValue& value)
    {
        const vfx::VFXExposedParameter* parameter = vfx::findExposedParameter(name);
        if (!parameter || !vfx::valueMatchesType(value, parameter->type))
            return false;

        switch (parameter->target)
        {
        case vfx::VFXOverrideTarget::SpawnRate:             out.spawnRate = std::get<float>(value); break;
        case vfx::VFXOverrideTarget::Lifetime:              out.lifetime = std::get<float>(value); break;
        case vfx::VFXOverrideTarget::StartSize:             out.startSize = std::get<float>(value); break;
        case vfx::VFXOverrideTarget::StartSpeed:            out.startSpeed = std::get<float>(value); break;
        case vfx::VFXOverrideTarget::StretchMultiplier:     out.stretchMultiplier = std::get<float>(value); break;
        case vfx::VFXOverrideTarget::WindStrength:          out.windStrength = std::get<float>(value); break;
        case vfx::VFXOverrideTarget::GravityStrength:       out.gravityStrength = std::get<float>(value); break;
        case vfx::VFXOverrideTarget::SoftParticleDistance:  out.softParticleDistance = std::get<float>(value); break;
        case vfx::VFXOverrideTarget::LightingInfluence:     out.lightingInfluence = std::get<float>(value); break;
        case vfx::VFXOverrideTarget::CollisionLifetimeLoss: out.collisionLifetimeLoss = std::get<float>(value); break;
        case vfx::VFXOverrideTarget::ConeSpread:            out.coneSpread = std::get<float>(value); break;
        case vfx::VFXOverrideTarget::RenderMode:            out.renderMode = static_cast<int>(std::get<int32_t>(value)); break;
        case vfx::VFXOverrideTarget::CollisionEnabled:      out.collisionEnabled = std::get<bool>(value); break;
        case vfx::VFXOverrideTarget::EmitDirection:         out.emitDirection = std::get<glm::vec3>(value); break;
        case vfx::VFXOverrideTarget::WindDirection:         out.windDirection = std::get<glm::vec3>(value); break;
        case vfx::VFXOverrideTarget::GravityDirection:      out.gravityDirection = std::get<glm::vec3>(value); break;
        case vfx::VFXOverrideTarget::ShapeDimensions:       out.shapeDimensions = std::get<glm::vec3>(value); break;
        case vfx::VFXOverrideTarget::StartColor:            out.startColor = std::get<glm::vec4>(value); break;
        default:                                            return false;
        }

        return true;
    }

    inline bool applyOverride(VFXEmitterOverrides& out, const vfx::VFXParamOverride& overrideValue)
    {
        return applyOverride(out, overrideValue.name, overrideValue.value);
    }

    inline bool applyScalarOverride(VFXEmitterOverrides& out, std::string_view name, float value)
    {
        const vfx::VFXExposedParameter* parameter = vfx::findExposedParameter(name);
        if (!parameter)
            return false;

        switch (parameter->target)
        {
        case vfx::VFXOverrideTarget::SpawnRate:             out.spawnRate = value; break;
        case vfx::VFXOverrideTarget::Lifetime:              out.lifetime = value; break;
        case vfx::VFXOverrideTarget::StartSize:             out.startSize = value; break;
        case vfx::VFXOverrideTarget::StartSpeed:            out.startSpeed = value; break;
        case vfx::VFXOverrideTarget::StretchMultiplier:     out.stretchMultiplier = value; break;
        case vfx::VFXOverrideTarget::WindStrength:          out.windStrength = value; break;
        case vfx::VFXOverrideTarget::GravityStrength:       out.gravityStrength = value; break;
        case vfx::VFXOverrideTarget::SoftParticleDistance:  out.softParticleDistance = value; break;
        case vfx::VFXOverrideTarget::LightingInfluence:     out.lightingInfluence = value; break;
        case vfx::VFXOverrideTarget::CollisionLifetimeLoss: out.collisionLifetimeLoss = value; break;
        case vfx::VFXOverrideTarget::ConeSpread:            out.coneSpread = value; break;
        case vfx::VFXOverrideTarget::RenderMode:            out.renderMode = static_cast<int>(value); break;
        case vfx::VFXOverrideTarget::CollisionEnabled:      out.collisionEnabled = (value != 0.0f); break;
        default:                                            return false;
        }

        return true;
    }

    inline bool applyVectorOverride(VFXEmitterOverrides& out, std::string_view name, const glm::vec4& value)
    {
        const vfx::VFXExposedParameter* parameter = vfx::findExposedParameter(name);
        if (!parameter)
            return false;

        switch (parameter->target)
        {
        case vfx::VFXOverrideTarget::EmitDirection:    out.emitDirection = glm::vec3(value); break;
        case vfx::VFXOverrideTarget::WindDirection:    out.windDirection = glm::vec3(value); break;
        case vfx::VFXOverrideTarget::GravityDirection: out.gravityDirection = glm::vec3(value); break;
        case vfx::VFXOverrideTarget::ShapeDimensions:  out.shapeDimensions = glm::vec3(value); break;
        case vfx::VFXOverrideTarget::StartColor:       out.startColor = value; break;
        default:                                       return false;
        }

        return true;
    }

    inline VFXEmitterOverrides toEmitterOverrides(std::span<const vfx::VFXParamOverride> overrides)
    {
        VFXEmitterOverrides out;
        for (const auto& overrideValue : overrides)
            applyOverride(out, overrideValue);
        return out;
    }

    inline VFXEmitterOverrides toEmitterOverrides(const std::vector<vfx::VFXParamOverride>& overrides)
    {
        return toEmitterOverrides(std::span<const vfx::VFXParamOverride>(overrides.data(), overrides.size()));
    }

    inline VFXEmitterOverrides toEmitterOverrides(const std::vector<std::pair<std::string, float>>& scalarOverrides,
                                                  const std::vector<std::pair<std::string, glm::vec4>>& vectorOverrides)
    {
        VFXEmitterOverrides out;
        for (const auto& [name, value] : scalarOverrides)
            applyScalarOverride(out, name, value);
        for (const auto& [name, value] : vectorOverrides)
            applyVectorOverride(out, name, value);
        return out;
    }
}
