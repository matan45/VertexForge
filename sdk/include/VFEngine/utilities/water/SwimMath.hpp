#pragma once

#include "BuoyancySampling.hpp"

#include <algorithm>
#include <cmath>

namespace water
{
    // VK-1606: buoyancy for Jolt CharacterVirtual controllers. Rigid bodies get their buoyancy from
    // OceanService::updateBuoyancy (force accumulation into the physics solver); a character
    // controller has no solver body to push, so its vertical velocity is integrated directly by
    // ControllerServiceImpl and this header supplies the terms that replace gravity while swimming.
    //
    // There is no GPU twin - this is CPU-only and has nothing to do with how water is drawn.

    // Fraction of a capsule's vertical extent below the surface, in [0, 1]. Expressed through the
    // existing computeSubmersion primitive (bottom of the capsule, span = full height) so a swimmer
    // and a floating crate agree on what "half submerged" means.
    inline float capsuleSubmersion(float centerY, float halfHeight, float waterHeight)
    {
        const float h = std::max(halfHeight, 0.0001f);
        return computeSubmersion(centerY - h, waterHeight, 2.0f * h);
    }

    // Capsule-centre height at which the character floats with `floatDepth` metres of itself under
    // water. floatDepth is clamped to the capsule so an over-large authored value cannot park the
    // character above the surface.
    inline float swimTargetCenterY(float waterHeight, float halfHeight, float floatDepth)
    {
        const float h = std::max(halfHeight, 0.0f);
        return waterHeight + h - std::clamp(floatDepth, 0.0f, 2.0f * h);
    }

    // One critically-damped spring step toward targetY, solved with BACKWARD Euler:
    //
    //     v' = (v - dt*w^2*(y - yTarget)) / (1 + w*dt)^2      with w = sqrt(stiffness)
    //
    // (the denominator is 1 + 2*w*dt + w^2*dt^2, which factors exactly). Backward Euler is
    // unconditionally stable here, so a hitch cannot make a swimmer explode and no omega*dt clamp is
    // needed - the semi-implicit form would have required one. Critically damped means it never
    // overshoots the target, so the character settles at the waterline instead of bobbing.
    //
    // Returns the NEW vertical velocity; the caller integrates the position with it.
    inline float buoyancyStep(float currentY, float targetY, float verticalVelocity,
                              float stiffness, float dt)
    {
        if (dt <= 0.0f)
            return verticalVelocity;

        const float omega = std::sqrt(std::max(stiffness, 0.0f));
        const float denom = 1.0f + omega * dt;
        return (verticalVelocity - dt * omega * omega * (currentY - targetY)) / (denom * denom);
    }

    // Exponential drag. exp() is always positive, so this scales the velocity toward zero and can
    // never flip its sign - which a naive `v -= v*drag*dt` does as soon as drag*dt exceeds 1, making
    // a swimmer vibrate at low frame rates.
    inline float applyWaterDrag(float velocity, float dragPerSecond, float dt)
    {
        if (dt <= 0.0f)
            return velocity;
        return velocity * std::exp(-std::max(dragPerSecond, 0.0f) * dt);
    }

    // Hysteresis band around the swim threshold: entering costs `enter` submersion, leaving needs to
    // drop a little below it. Without this a character standing exactly at the threshold flickers
    // between the Swim and Walk locomotion states every frame.
    inline bool swimStateFor(bool wasSwimming, float submersion, float enterThreshold,
                             float exitMargin = 0.1f)
    {
        const float enter = std::clamp(enterThreshold, 0.0f, 1.0f);
        const float exit = std::max(enter - std::max(exitMargin, 0.0f), 0.0f);
        return wasSwimming ? (submersion > exit) : (submersion >= enter);
    }
}
