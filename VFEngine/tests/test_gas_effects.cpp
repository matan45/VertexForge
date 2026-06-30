#include <doctest.h>

// VK-816 (Gameplay Ability System) — gameplay effect runtime: instant/duration/infinite,
// periodic firing, granted-tag lifetime, and the four stacking behaviors.

#include "../../plugins/GameplayAbilitySystem/gas/core/EffectRuntime.hpp"

using namespace gas;

namespace
{
    Attribute attr(float base, float lo = 0.0f, float hi = 1e30f)
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

TEST_SUITE("GAS.Effects")
{
    TEST_CASE("Instant effect modifies base permanently and is not stored")
    {
        EffectBook book;
        book.attributes().setAttribute("Health", attr(100.0f, 0.0f, 1000.0f));
        book.recomputeAttributes();

        GameplayEffectSpec heal;
        heal.id = "Heal";
        heal.durationPolicy = DurationPolicy::Instant;
        heal.modifiers = {mod("Health", ModifierOp::Add, 25.0f)};

        auto r = book.apply(heal);
        CHECK(r.applied);
        CHECK(r.handle == 0); // instant => not stored
        CHECK(book.attributes().baseOf("Health") == doctest::Approx(125.0f));
        CHECK(book.attributes().currentOf("Health") == doctest::Approx(125.0f));
        CHECK(book.active().empty());
    }

    TEST_CASE("Duration effect: continuous modifier reverts and granted tags drop on expiry")
    {
        EffectBook book;
        book.attributes().setAttribute("Health", attr(100.0f, 0.0f, 1000.0f));
        book.recomputeAttributes();

        GameplayEffectSpec buff;
        buff.id = "Buff";
        buff.durationPolicy = DurationPolicy::Duration;
        buff.duration = 2.0f;
        buff.modifiers = {mod("Health", ModifierOp::Add, 50.0f)};
        buff.grantedTags = {"Character.Buffed"};

        auto r = book.apply(buff);
        CHECK(r.applied);
        CHECK(r.handle != 0);
        CHECK(book.attributes().currentOf("Health") == doctest::Approx(150.0f));
        CHECK(book.tags().hasTag("Character.Buffed"));

        auto t1 = book.tick(1.0f);
        CHECK(t1.expired.empty());
        CHECK(book.attributes().currentOf("Health") == doctest::Approx(150.0f));

        auto t2 = book.tick(1.5f); // timeRemaining 1.0 -> -0.5 => expire
        REQUIRE(t2.expired.size() == 1);
        CHECK(t2.expired[0] == r.handle);
        CHECK(book.attributes().currentOf("Health") == doctest::Approx(100.0f)); // reverted
        CHECK_FALSE(book.tags().hasTag("Character.Buffed"));                      // tag dropped
        CHECK(book.active().empty());
    }

    TEST_CASE("granted tags are reference-counted across overlapping effects")
    {
        EffectBook book;

        GameplayEffectSpec a;
        a.id = "StunA";
        a.durationPolicy = DurationPolicy::Duration;
        a.duration = 1.0f;
        a.grantedTags = {"Character.Stunned"};

        GameplayEffectSpec b = a;
        b.id = "StunB";
        b.duration = 3.0f;

        auto ra = book.apply(a);
        auto rb = book.apply(b);
        CHECK(book.tags().hasTag("Character.Stunned"));

        book.tick(1.5f); // A expires, B remains
        CHECK(book.tags().hasTag("Character.Stunned")); // B still grants it

        book.tick(2.0f); // B expires
        CHECK_FALSE(book.tags().hasTag("Character.Stunned"));
    }

    TEST_CASE("Infinite effect persists through ticks until explicitly removed")
    {
        EffectBook book;
        book.attributes().setAttribute("Mana", attr(50.0f, 0.0f, 1000.0f));
        book.recomputeAttributes();

        GameplayEffectSpec aura;
        aura.id = "Aura";
        aura.durationPolicy = DurationPolicy::Infinite;
        aura.modifiers = {mod("Mana", ModifierOp::Add, 10.0f)};

        auto r = book.apply(aura);
        CHECK(book.attributes().currentOf("Mana") == doctest::Approx(60.0f));

        auto t = book.tick(1000.0f);
        CHECK(t.expired.empty());
        CHECK(book.attributes().currentOf("Mana") == doctest::Approx(60.0f));

        auto rem = book.remove(r.handle);
        CHECK(rem.removed);
        CHECK(book.attributes().currentOf("Mana") == doctest::Approx(50.0f));
    }

    TEST_CASE("periodic effect fires its modifiers against base each period")
    {
        EffectBook book;
        book.attributes().setAttribute("Health", attr(100.0f, 0.0f, 1000.0f));
        book.recomputeAttributes();

        GameplayEffectSpec regen;
        regen.id = "Regen";
        regen.durationPolicy = DurationPolicy::Duration;
        regen.duration = 10.0f;
        regen.period = 1.0f;
        regen.modifiers = {mod("Health", ModifierOp::Add, 5.0f)};

        book.apply(regen);
        // No immediate fire on application.
        CHECK(book.attributes().currentOf("Health") == doctest::Approx(100.0f));

        auto t1 = book.tick(1.0f);
        CHECK(t1.periodicFires.size() == 1);
        CHECK(book.attributes().baseOf("Health") == doctest::Approx(105.0f));
        CHECK(book.attributes().currentOf("Health") == doctest::Approx(105.0f));

        auto t2 = book.tick(2.5f); // two whole periods elapse
        CHECK(t2.periodicFires.size() == 2);
        CHECK(book.attributes().baseOf("Health") == doctest::Approx(115.0f));
        CHECK(book.active().size() == 1); // still active (duration 10)
    }

    TEST_CASE("stacking None rejects further applications at the limit")
    {
        EffectBook book;
        GameplayEffectSpec poison;
        poison.id = "Poison";
        poison.durationPolicy = DurationPolicy::Duration;
        poison.duration = 5.0f;
        poison.stacking.policy = StackingPolicy::None;
        poison.stacking.limit = 1;

        auto r1 = book.apply(poison);
        CHECK(r1.applied);
        auto r2 = book.apply(poison);
        CHECK_FALSE(r2.applied);
        CHECK(r2.handle == 0);
        CHECK(book.active().size() == 1);
    }

    TEST_CASE("stacking BySource increments to the limit; different source is a separate stack")
    {
        EffectBook book;
        book.attributes().setAttribute("AP", attr(0.0f, 0.0f, 100.0f));
        book.recomputeAttributes();

        GameplayEffectSpec st;
        st.id = "Stacks";
        st.durationPolicy = DurationPolicy::Duration;
        st.duration = 5.0f;
        st.stacking.policy = StackingPolicy::BySource;
        st.stacking.limit = 3;
        st.modifiers = {mod("AP", ModifierOp::Add, 1.0f)};

        book.apply(st, /*source*/ 1);
        book.apply(st, 1);
        book.apply(st, 1);
        CHECK(book.attributes().currentOf("AP") == doctest::Approx(3.0f));

        auto over = book.apply(st, 1); // at limit, no durationRefresh => rejected
        CHECK_FALSE(over.applied);
        CHECK(book.attributes().currentOf("AP") == doctest::Approx(3.0f));

        book.apply(st, /*source*/ 2); // distinct source => new stack
        REQUIRE(book.active().size() == 2);
        CHECK(book.active()[0].stackCount == 3);
        CHECK(book.active()[1].stackCount == 1);
        CHECK(book.attributes().currentOf("AP") == doctest::Approx(4.0f));
    }

    TEST_CASE("stacking ByTarget ignores source and increments one shared stack")
    {
        EffectBook book;
        book.attributes().setAttribute("AP", attr(0.0f, 0.0f, 100.0f));
        book.recomputeAttributes();

        GameplayEffectSpec bt;
        bt.id = "Shared";
        bt.durationPolicy = DurationPolicy::Duration;
        bt.duration = 5.0f;
        bt.stacking.policy = StackingPolicy::ByTarget;
        bt.stacking.limit = 3;
        bt.modifiers = {mod("AP", ModifierOp::Add, 1.0f)};

        book.apply(bt, 1);
        book.apply(bt, 2);
        book.apply(bt, 3);
        REQUIRE(book.active().size() == 1);
        CHECK(book.active()[0].stackCount == 3);
        CHECK(book.attributes().currentOf("AP") == doctest::Approx(3.0f));

        auto over = book.apply(bt, 4); // at limit
        CHECK_FALSE(over.applied);
        CHECK(book.active()[0].stackCount == 3);
    }

    TEST_CASE("durationRefresh resets timeRemaining on a stacking application")
    {
        EffectBook book;
        GameplayEffectSpec dr;
        dr.id = "Refresh";
        dr.durationPolicy = DurationPolicy::Duration;
        dr.duration = 5.0f;
        dr.stacking.policy = StackingPolicy::BySource;
        dr.stacking.limit = 3;
        dr.stacking.durationRefresh = true;

        book.apply(dr, 1);
        book.tick(2.0f);
        CHECK(book.active()[0].timeRemaining == doctest::Approx(3.0f));

        book.apply(dr, 1); // increment + refresh
        CHECK(book.active()[0].stackCount == 2);
        CHECK(book.active()[0].timeRemaining == doctest::Approx(5.0f));
    }

    TEST_CASE("expiration ClearStack removes the whole effect")
    {
        EffectBook book;
        book.attributes().setAttribute("X", attr(0.0f, 0.0f, 100.0f));
        book.recomputeAttributes();

        GameplayEffectSpec cs;
        cs.id = "ClearMe";
        cs.durationPolicy = DurationPolicy::Duration;
        cs.duration = 2.0f;
        cs.stacking.policy = StackingPolicy::BySource;
        cs.stacking.limit = 3;
        cs.stacking.expiration = StackExpiration::ClearStack;
        cs.modifiers = {mod("X", ModifierOp::Add, 1.0f)};

        book.apply(cs, 1);
        book.apply(cs, 1); // stackCount 2, X == 2
        CHECK(book.attributes().currentOf("X") == doctest::Approx(2.0f));

        auto t = book.tick(3.0f); // expire whole stack
        CHECK(t.expired.size() == 1);
        CHECK(book.active().empty());
        CHECK(book.attributes().currentOf("X") == doctest::Approx(0.0f));
    }

    TEST_CASE("expiration RemoveSingle drops one stack and refreshes until the last expires")
    {
        EffectBook book;
        book.attributes().setAttribute("X", attr(0.0f, 0.0f, 100.0f));
        book.recomputeAttributes();

        GameplayEffectSpec rs;
        rs.id = "Peel";
        rs.durationPolicy = DurationPolicy::Duration;
        rs.duration = 2.0f;
        rs.stacking.policy = StackingPolicy::BySource;
        rs.stacking.limit = 3;
        rs.stacking.expiration = StackExpiration::RemoveSingle;
        rs.modifiers = {mod("X", ModifierOp::Add, 1.0f)};

        book.apply(rs, 1);
        book.apply(rs, 1); // stackCount 2
        CHECK(book.attributes().currentOf("X") == doctest::Approx(2.0f));

        auto t1 = book.tick(3.0f); // peel one, refresh, persist
        CHECK(t1.expired.empty());
        REQUIRE(book.active().size() == 1);
        CHECK(book.active()[0].stackCount == 1);
        CHECK(book.active()[0].timeRemaining == doctest::Approx(2.0f));
        CHECK(book.attributes().currentOf("X") == doctest::Approx(1.0f));

        auto t2 = book.tick(3.0f); // last stack expires
        CHECK(t2.expired.size() == 1);
        CHECK(book.active().empty());
        CHECK(book.attributes().currentOf("X") == doctest::Approx(0.0f));
    }

    TEST_CASE("cooldown is just a Duration effect; getCooldownRemaining tracks the max")
    {
        EffectBook book;
        auto cd = EffectBook::makeCooldownEffect("Fireball.Cooldown", 5.0f, {"Cooldown.Fireball"});
        book.apply(cd);

        CHECK(book.tags().hasTag("Cooldown.Fireball"));
        CHECK(book.getCooldownRemaining({"Cooldown.Fireball"}) == doctest::Approx(5.0f));
        CHECK(book.getCooldownRemaining({"Cooldown.Other"}) == doctest::Approx(0.0f));

        book.tick(2.0f);
        CHECK(book.getCooldownRemaining({"Cooldown.Fireball"}) == doctest::Approx(3.0f));

        book.tick(3.0f); // cooldown effect expires
        CHECK(book.getCooldownRemaining({"Cooldown.Fireball"}) == doctest::Approx(0.0f));
        CHECK_FALSE(book.tags().hasTag("Cooldown.Fireball"));
    }
}
