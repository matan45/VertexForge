// CPU-only coverage for the VK-1453 (Phase 4) per-effect scalability resolver in
// VFXScalability.hpp:
//
//   * A disabled profile resolves to the neutral level (spawnRateScale=1,
//     maxParticles=-1, cullDistance=-1, updateInterval=1, rendererEnabled=true)
//     regardless of what its levels contain — existing effects are unaffected.
//   * An enabled profile returns the level for the requested tier.
//   * An out-of-range tier index is clamped to the top tier.
//
// Pure function of VFXScalability — no render/GPU types, no Vulkan device.

#include <doctest.h>

#include <vfx/VFXScalability.hpp>

namespace
{
    // A profile with a distinct value in every field of every tier so a wrong
    // index or field is caught.
    vfx::VFXScalability makeDistinctProfile()
    {
        vfx::VFXScalability profile;
        profile.enabled = true;
        for (int i = 0; i < vfx::kVFXQualityTierCount; ++i)
        {
            vfx::VFXScalabilityLevel& level = profile.levels[i];
            level.spawnRateScale = 0.1f * static_cast<float>(i + 1);
            level.maxParticles = 10 * (i + 1);
            level.cullDistance = 100.0f * static_cast<float>(i + 1);
            level.updateInterval = i + 1;
            level.rendererEnabled = (i != 0);
        }
        return profile;
    }

    void checkLevelEqual(const vfx::VFXScalabilityLevel& a, const vfx::VFXScalabilityLevel& b)
    {
        CHECK(a.spawnRateScale == doctest::Approx(b.spawnRateScale));
        CHECK(a.maxParticles == b.maxParticles);
        CHECK(a.cullDistance == doctest::Approx(b.cullDistance));
        CHECK(a.updateInterval == b.updateInterval);
        CHECK(a.rendererEnabled == b.rendererEnabled);
    }
}

TEST_SUITE("VFXScalability")
{
    TEST_CASE("a disabled profile resolves to the neutral level regardless of levels")
    {
        vfx::VFXScalability profile = makeDistinctProfile();
        profile.enabled = false; // authored levels must be ignored

        const vfx::VFXScalabilityLevel neutral{}; // struct defaults
        for (int i = 0; i < vfx::kVFXQualityTierCount; ++i)
        {
            const auto resolved = vfx::resolveScalability(profile, static_cast<vfx::VFXQualityTier>(i));
            checkLevelEqual(resolved, neutral);
        }

        // Spell out the neutral defaults explicitly.
        const auto resolved = vfx::resolveScalability(profile, vfx::VFXQualityTier::Low);
        CHECK(resolved.spawnRateScale == doctest::Approx(1.0f));
        CHECK(resolved.maxParticles == -1);
        CHECK(resolved.cullDistance == doctest::Approx(-1.0f));
        CHECK(resolved.updateInterval == 1);
        CHECK(resolved.rendererEnabled == true);
    }

    TEST_CASE("an enabled profile returns the level for the requested tier")
    {
        const vfx::VFXScalability profile = makeDistinctProfile();

        checkLevelEqual(vfx::resolveScalability(profile, vfx::VFXQualityTier::Low), profile.levels[0]);
        checkLevelEqual(vfx::resolveScalability(profile, vfx::VFXQualityTier::Medium), profile.levels[1]);
        checkLevelEqual(vfx::resolveScalability(profile, vfx::VFXQualityTier::High), profile.levels[2]);
        checkLevelEqual(vfx::resolveScalability(profile, vfx::VFXQualityTier::Ultra), profile.levels[3]);
    }

    TEST_CASE("an out-of-range tier is clamped to the top tier")
    {
        const vfx::VFXScalability profile = makeDistinctProfile();

        // Values past Ultra (3) clamp to the last tier.
        checkLevelEqual(vfx::resolveScalability(profile, static_cast<vfx::VFXQualityTier>(7)),
                        profile.levels[vfx::kVFXQualityTierCount - 1]);
        checkLevelEqual(vfx::resolveScalability(profile, static_cast<vfx::VFXQualityTier>(255)),
                        profile.levels[vfx::kVFXQualityTierCount - 1]);
    }
}
