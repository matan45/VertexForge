#include <doctest.h>

// VK-1404: RTS combat damage rule. The Tests project links no plugin code and has
// no plugins/ include dir, so we reach the header-only rule via a relative path
// from this file. CombatRules.hpp + RTSComponents.hpp are self-contained (only
// <algorithm>), so no extra include dir or link is required. The plugin native
// (_rts_apply_damage) and this test both call applyDamageToHealth — testing the
// pure rule here covers the damage/kill/clamp logic without an ECS or DLL.
#include "../../plugins/RTSGameplay/CombatRules.hpp"

TEST_SUITE("RTSCombat")
{
    TEST_CASE("applyDamageToHealth decrements HP by the damage amount")
    {
        HealthComponent h;
        h.maxHP = 100.0f;
        h.currentHP = 100.0f;

        const DamageResult r = applyDamageToHealth(h, 30.0f);

        CHECK(r == DamageResult::Survived);
        CHECK(h.currentHP == doctest::Approx(70.0f));
    }

    TEST_CASE("applyDamageToHealth clamps at 0 (no negative HP)")
    {
        HealthComponent h;
        h.currentHP = 20.0f;

        const DamageResult r = applyDamageToHealth(h, 75.0f);

        CHECK(r == DamageResult::Killed);
        CHECK(h.currentHP == doctest::Approx(0.0f));
        CHECK(h.currentHP >= 0.0f);
    }

    TEST_CASE("applyDamageToHealth reports Killed exactly when HP reaches 0")
    {
        HealthComponent h;
        h.currentHP = 25.0f;

        SUBCASE("exact lethal damage kills")
        {
            const DamageResult r = applyDamageToHealth(h, 25.0f);
            CHECK(r == DamageResult::Killed);
            CHECK(h.currentHP == doctest::Approx(0.0f));
        }

        SUBCASE("non-lethal damage survives")
        {
            const DamageResult r = applyDamageToHealth(h, 24.0f);
            CHECK(r == DamageResult::Survived);
            CHECK(h.currentHP == doctest::Approx(1.0f));
        }
    }

    TEST_CASE("applyDamageToHealth is a no-op for non-positive amounts")
    {
        HealthComponent h;
        h.currentHP = 50.0f;

        SUBCASE("zero damage")
        {
            const DamageResult r = applyDamageToHealth(h, 0.0f);
            CHECK(r == DamageResult::Survived);
            CHECK(h.currentHP == doctest::Approx(50.0f));
        }

        SUBCASE("negative damage does not heal")
        {
            const DamageResult r = applyDamageToHealth(h, -25.0f);
            CHECK(r == DamageResult::Survived);
            CHECK(h.currentHP == doctest::Approx(50.0f));
        }
    }

    TEST_CASE("applyDamageToHealth does not re-kill an already-dead target")
    {
        // Regression: a dead-but-not-yet-cleaned-up unit hit by AoE/aggro must
        // report Survived (HP stays 0), so "rts.unit_killed" fires only once.
        HealthComponent h;
        h.currentHP = 0.0f;

        const DamageResult r = applyDamageToHealth(h, 50.0f);

        CHECK(r == DamageResult::Survived);
        CHECK(h.currentHP == doctest::Approx(0.0f));
    }

    TEST_CASE("DamageResult enum values match the native int contract")
    {
        // _rts_apply_damage returns these ints: -1 no Health, 0 survived, 1 killed.
        CHECK(static_cast<int>(DamageResult::NoHealth) == -1);
        CHECK(static_cast<int>(DamageResult::Survived) == 0);
        CHECK(static_cast<int>(DamageResult::Killed) == 1);
    }
}
