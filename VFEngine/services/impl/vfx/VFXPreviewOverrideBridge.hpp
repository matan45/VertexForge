#pragma once

#include "../../data/VFXTypes.hpp"
#include "../../providers/vfx/IVFXPreviewProvider.hpp"

#include <vfx/VFXForceTypes.hpp>
#include <vfx/VFXShapeTypes.hpp>

#include <variant>

namespace services
{
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
    }
}
