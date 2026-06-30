#include <doctest.h>

// VK-816 (Gameplay Ability System) — attribute aggregation order, clamping and delta reporting.

#include "../../plugins/GameplayAbilitySystem/gas/core/Attributes.hpp"
#include "../../plugins/GameplayAbilitySystem/gas/core/Aggregator.hpp"

using namespace gas;

namespace
{
    Attribute attr(float base, float lo = -1e30f, float hi = 1e30f)
    {
        Attribute a;
        a.base = base;
        a.minValue = lo;
        a.maxValue = hi;
        return a;
    }

    AttributeModifier mod(const std::string& name, ModifierOp op, float mag)
    {
        AttributeModifier m;
        m.attribute = name;
        m.op = op;
        m.magnitude = mag;
        return m;
    }
}

TEST_SUITE("GAS.Aggregation")
{
    TEST_CASE("Add modifiers sum into base")
    {
        AttributeState st;
        st.setAttribute("Health", attr(100.0f));

        auto changes = Aggregator::recompute(st, {mod("Health", ModifierOp::Add, 25.0f),
                                                  mod("Health", ModifierOp::Add, 15.0f)});
        CHECK(st.currentOf("Health") == doctest::Approx(140.0f));
        REQUIRE(changes.size() == 1);
        CHECK(changes[0].attribute == "Health");
        CHECK(changes[0].oldValue == doctest::Approx(100.0f));
        CHECK(changes[0].newValue == doctest::Approx(140.0f));
    }

    TEST_CASE("Multiply modifiers are direct factors (1.5 == +50%)")
    {
        AttributeState st;
        st.setAttribute("Speed", attr(10.0f));

        Aggregator::recompute(st, {mod("Speed", ModifierOp::Multiply, 1.5f),
                                   mod("Speed", ModifierOp::Multiply, 2.0f)});
        // (10 + 0) * 1.5 * 2.0 = 30
        CHECK(st.currentOf("Speed") == doctest::Approx(30.0f));
    }

    TEST_CASE("Add then Multiply: (base + sumAdd) * product(Multiply)")
    {
        AttributeState st;
        st.setAttribute("Attack", attr(100.0f));

        Aggregator::recompute(st, {mod("Attack", ModifierOp::Add, 50.0f),
                                   mod("Attack", ModifierOp::Multiply, 2.0f)});
        // (100 + 50) * 2 = 300, regardless of modifier declaration order.
        CHECK(st.currentOf("Attack") == doctest::Approx(300.0f));
    }

    TEST_CASE("Override wins over Add/Multiply; last override applied wins")
    {
        AttributeState st;
        st.setAttribute("Armor", attr(100.0f, 0.0f, 1000.0f));

        Aggregator::recompute(st, {mod("Armor", ModifierOp::Add, 50.0f),
                                   mod("Armor", ModifierOp::Multiply, 4.0f),
                                   mod("Armor", ModifierOp::Override, 10.0f),
                                   mod("Armor", ModifierOp::Override, 42.0f)});
        CHECK(st.currentOf("Armor") == doctest::Approx(42.0f));
    }

    TEST_CASE("result is clamped into [min, max]")
    {
        AttributeState st;
        st.setAttribute("Health", attr(100.0f, 0.0f, 80.0f));

        Aggregator::recompute(st, {mod("Health", ModifierOp::Add, 1000.0f)});
        CHECK(st.currentOf("Health") == doctest::Approx(80.0f)); // clamped to max

        st.setAttribute("Mana", attr(50.0f, 0.0f, 100.0f));
        Aggregator::recompute(st, {mod("Mana", ModifierOp::Add, -1000.0f)});
        CHECK(st.currentOf("Mana") == doctest::Approx(0.0f));    // clamped to min
    }

    TEST_CASE("changes are reported only on delta")
    {
        AttributeState st;
        st.setAttribute("Health", attr(100.0f, 0.0f, 1000.0f));

        auto first = Aggregator::recompute(st, {mod("Health", ModifierOp::Add, 50.0f)});
        CHECK(first.size() == 1); // 100 -> 150

        auto second = Aggregator::recompute(st, {mod("Health", ModifierOp::Add, 50.0f)});
        CHECK(second.empty());    // 150 -> 150, no delta

        auto third = Aggregator::recompute(st, {});
        CHECK(third.size() == 1); // 150 -> 100 (mods removed)
        CHECK(st.currentOf("Health") == doctest::Approx(100.0f));
    }

    TEST_CASE("attribute-driven max is evaluated first (provider settles before dependent)")
    {
        AttributeState st;
        // MaxHealth has static bounds and is computed in wave A.
        st.setAttribute("MaxHealth", attr(80.0f));
        // Health's max is driven by MaxHealth's current value.
        Attribute health = attr(100.0f, 0.0f);
        health.maxAttribute = "MaxHealth";
        st.setAttribute("Health", health);

        Aggregator::recompute(st, {});
        // Health is clamped to MaxHealth.current (80), not its own +inf static max.
        CHECK(st.currentOf("Health") == doctest::Approx(80.0f));

        // Raise the cap; Health may now reach its base of 100.
        st.setBase("MaxHealth", 120.0f);
        Aggregator::recompute(st, {});
        CHECK(st.currentOf("MaxHealth") == doctest::Approx(120.0f));
        CHECK(st.currentOf("Health") == doctest::Approx(100.0f));
    }
}
