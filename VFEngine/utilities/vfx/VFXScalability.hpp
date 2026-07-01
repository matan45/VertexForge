#pragma once

// VK-1453 (VFXSequence Phase 4) — per-effect scalability profile.
//
// An embedded, per-quality-tier scalability block on a .vfVFX asset. A single global
// tier (derived from the renderer's RenderPreset) selects one level at instance
// creation. Neutral defaults leave an effect unchanged, and a disabled profile
// resolves to a neutral level, so behavior is byte-identical when unused. Pure /
// CPU-testable — the renderer applies the resolved level to the base emitter config.

#include <algorithm>
#include <cstdint>

namespace vfx
{
    // Global VFX quality tier. Mapped from types::RenderPreset
    // (Low/Medium/High/Ultra/Custom -> tier) and read once per instance creation.
    enum class VFXQualityTier : uint8_t
    {
        Low = 0,
        Medium = 1,
        High = 2,
        Ultra = 3
    };

    inline constexpr int kVFXQualityTierCount = 4;

    // Per-tier scalability knobs for one effect. Neutral defaults (spawnRateScale=1,
    // no particle/cull override, every-frame update, renderer enabled) leave the
    // effect unchanged.
    struct VFXScalabilityLevel
    {
        float spawnRateScale = 1.0f;   // multiplies the emitter spawn rate
        int   maxParticles = -1;       // <0 => no cap (use asset/emitter default)
        float cullDistance = -1.0f;    // <0 => use the global VFX cull distance
        int   updateInterval = 1;      // >1 => simulate only every Nth frame
        bool  rendererEnabled = true;  // false => effect is not spawned/drawn at this tier
    };

    // Embedded per-asset scalability profile. Disabled by default so resolve()
    // returns a neutral level and existing assets are unaffected.
    struct VFXScalability
    {
        bool enabled = false;
        VFXScalabilityLevel levels[kVFXQualityTierCount];
    };

    // Select the level for the active tier. A disabled profile yields the neutral
    // default (no behavioral change).
    inline VFXScalabilityLevel resolveScalability(const VFXScalability& profile, VFXQualityTier tier)
    {
        if (!profile.enabled)
            return VFXScalabilityLevel{};
        const int idx = std::clamp(static_cast<int>(tier), 0, kVFXQualityTierCount - 1);
        return profile.levels[idx];
    }
}
