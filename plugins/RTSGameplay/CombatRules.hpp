#pragma once

#include "RTSComponents.hpp"

#include <algorithm>

// Pure combat damage rule (VK-1404), kept free of ECS/plugin dependencies so
// both the _rts_apply_damage native and the unit tests can exercise it directly
// (the Tests project links no plugin code — it includes this header only).

// Outcome of applying damage to a HealthComponent. Mirrors the int contract of
// the _rts_apply_damage native: -1 no Health, 0 survived (incl. no-op), 1 killed.
enum class DamageResult
{
    NoHealth = -1,
    Survived = 0,
    Killed   = 1
};

// Apply `amount` of damage to `health`, clamping currentHP at 0. Non-positive
// amounts are a no-op (Survived, HP unchanged) — healing is a separate concern.
// Returns Killed only on the transition to 0 HP; a target already at 0 survives.
inline DamageResult applyDamageToHealth(HealthComponent& health, float amount)
{
    // No-op for healing (amount <= 0) and for a target already at 0 HP, so a kill
    // is reported exactly once on the transition to 0 — repeated AoE/aggro hits on
    // a dead-but-not-yet-cleaned-up unit must not re-fire "rts.unit_killed".
    if (amount <= 0.0f || health.currentHP <= 0.0f)
        return DamageResult::Survived;

    health.currentHP = std::max(0.0f, health.currentHP - amount);
    return health.currentHP <= 0.0f ? DamageResult::Killed : DamageResult::Survived;
}
