#pragma once

#include "../../data/VFXTypes.hpp"
#include "../../providers/vfx/IVFXPreviewProvider.hpp"

#include <vfx/VFXForceTypes.hpp>
#include <vfx/VFXModifierTypes.hpp>
#include <vfx/VFXParameterRegistry.hpp>
#include <vfx/VFXShapeTypes.hpp>

#include <algorithm>
#include <variant>

namespace services
{
    // Applies the overrides to params in place. colorTint MULTIPLIES the colours, so params must
    // be freshly built from the asset before every call (the sequence editor rebuilds them).
    inline void applyToPreviewParams(VFXPreviewParams& params, const VFXEmitterOverrides& overrides)
    {
        if (overrides.spawnRate) params.spawnRate = *overrides.spawnRate;
        if (overrides.lifetime) params.lifetime = *overrides.lifetime;
        if (overrides.startSize) params.startSize = *overrides.startSize;
        if (overrides.startSpeed) params.startSpeed = *overrides.startSpeed;
        if (overrides.stretchMultiplier) params.stretchMultiplier = *overrides.stretchMultiplier;
        if (overrides.emitDirection) params.emitDirection = *overrides.emitDirection;
        if (overrides.startColor) params.startColor = *overrides.startColor;
        if (overrides.renderMode) params.renderMode = *overrides.renderMode;
        // VK-1657: a VFXBlendMode value, clamped to 0..3. sortOrder is not mapped: VFXPreviewParams
        // has no draw-order field.
        if (overrides.blendMode)
            params.blendMode = static_cast<::vfx::VFXBlendMode>(
                std::clamp(*overrides.blendMode, 0, static_cast<int>(::vfx::kBlendModeCount) - 1));
        if (overrides.softParticleDistance) params.softParticleDistance = *overrides.softParticleDistance;
        if (overrides.lightingInfluence) params.lightingInfluence = *overrides.lightingInfluence;
        if (overrides.collisionEnabled) params.collisionEnabled = *overrides.collisionEnabled;
        if (overrides.collisionLifetimeLoss) params.collisionLifetimeLoss = *overrides.collisionLifetimeLoss;

        if (overrides.windDirection || overrides.windStrength)
        {
            bool found = false;
            for (auto& force : params.forces.forces)
            {
                if (auto* wind = std::get_if<::vfx::WindForceConfig>(&force))
                {
                    if (overrides.windDirection) wind->direction = *overrides.windDirection;
                    if (overrides.windStrength) wind->strength = *overrides.windStrength;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                ::vfx::WindForceConfig wind;
                if (overrides.windDirection) wind.direction = *overrides.windDirection;
                if (overrides.windStrength) wind.strength = *overrides.windStrength;
                params.forces.forces.push_back(wind);
            }
        }

        if (overrides.gravityDirection || overrides.gravityStrength)
        {
            bool found = false;
            for (auto& force : params.forces.forces)
            {
                if (auto* gravity = std::get_if<::vfx::GravityForceConfig>(&force))
                {
                    if (overrides.gravityDirection) gravity->direction = *overrides.gravityDirection;
                    if (overrides.gravityStrength) gravity->strength = *overrides.gravityStrength;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                ::vfx::GravityForceConfig gravity;
                if (overrides.gravityDirection) gravity.direction = *overrides.gravityDirection;
                if (overrides.gravityStrength) gravity.strength = *overrides.gravityStrength;
                params.forces.forces.push_back(gravity);
            }
        }

        if (overrides.shapeDimensions)
        {
            params.shape.type = ::vfx::ShapeType::Box;
            params.shape.dimensions = glm::vec4(*overrides.shapeDimensions, 0.0f);
        }

        // VK-1657: the tint multiplies the start colour (after a startColor override) and every
        // ColorOverLifetime stop, like the scene renderer; ColorBySpeed and the ribbon tail
        // gradient multiply on top of that, so they stay authored.
        if (overrides.colorTint)
        {
            const glm::vec4 tint = glm::max(*overrides.colorTint, glm::vec4(0.0f));
            params.startColor *= tint;
            for (auto& modifier : params.modifiers.modifiers)
            {
                auto* colorOverLifetime = std::get_if<::vfx::ColorOverLifetimeConfig>(&modifier);
                if (!colorOverLifetime)
                    continue;
                // An empty gradient evaluates to white; one white stop keeps that and takes the tint.
                if (colorOverLifetime->gradient.stops.empty())
                    colorOverLifetime->gradient.stops.push_back(::vfx::VFXGradientStop{0.0f, glm::vec4(1.0f)});
                for (auto& stop : colorOverLifetime->gradient.stops)
                    stop.color *= tint;
            }
        }
    }
}
