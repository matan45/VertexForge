#include <doctest.h>

// VK-816 (Gameplay Ability System) — transactional ability activation: the success
// path, every failure branch, the validate-all-then-commit invariant (a failed plan
// leaves the owner byte-identical), and cost/cooldown gating over repeated casts.

#include "../../plugins/GameplayAbilitySystem/gas/core/ActivationPipeline.hpp"

#include <map>
#include <string>
#include <vector>

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

    // A self-contained activation scenario. ctx points into THIS fixture, so never
    // copy/move it after wire().
    struct Fixture
    {
        EffectBook book;
        AbilitySpec ability;
        std::map<std::string, GameplayEffectSpec> registry;
        ActivationContext ctx;

        void wire()
        {
            ctx.ability = &ability;
            ctx.ownerTags = &book.tags();
            ctx.ownerAttributes = &book.attributes();
            ctx.isGranted = true;
            ctx.cooldownRemaining = 0.0f;
            ctx.resolveEffect = [this](const std::string& id) -> const GameplayEffectSpec*
            {
                auto it = registry.find(id);
                return it == registry.end() ? nullptr : &it->second;
            };
        }
    };

    // A fully valid Fireball: requires CanCast, blocked by Stunned, costs 30 Mana,
    // 5s cooldown, grants Casting while active, applies Burn, fires a cue. Self-target.
    void buildFireball(Fixture& f)
    {
        f.book.attributes().setAttribute("Mana", attr(100.0f, 0.0f, 1000.0f));
        f.book.recomputeAttributes();
        f.book.tags().addTag("Character.CanCast");

        GameplayEffectSpec cost;
        cost.id = "Cost.Mana30";
        cost.durationPolicy = DurationPolicy::Instant;
        cost.modifiers = {mod("Mana", ModifierOp::Add, -30.0f)};
        f.registry[cost.id] = cost;

        GameplayEffectSpec burn;
        burn.id = "Effect.Burn";
        burn.durationPolicy = DurationPolicy::Duration;
        burn.duration = 3.0f;
        burn.grantedTags = {"Character.Burning"};
        f.registry[burn.id] = burn;

        AbilitySpec& ab = f.ability;
        ab.id = "Fireball";
        ab.activationBlockedTags = {"Character.Stunned"};
        ab.activationRequiredTags = {"Character.CanCast"};
        ab.cost.effectId = "Cost.Mana30";
        ab.cooldown.duration = 5.0f;
        ab.cooldown.cooldownTags = {"Cooldown.Fireball"};
        ab.activationOwnedTags = {"Character.Casting"};
        ab.effectsOnActivate = {"Effect.Burn"};
        ab.cueIds = {"Cue.Boom"};
        ab.targeting.type = TargetingType::Self;
    }
}

TEST_SUITE("GAS.Activation")
{
    TEST_CASE("success: plan is valid and commit applies cost, cooldown, effects and owned tags")
    {
        Fixture f;
        buildFireball(f);
        f.wire();

        auto plan = ActivationPipeline::planActivate(f.ctx);
        REQUIRE(plan.valid);
        CHECK(plan.status == ActivationStatus::Success);
        CHECK(plan.costEffect.has_value());
        CHECK(plan.cooldownEffect.has_value());
        REQUIRE(plan.effectsOnActivate.size() == 1);
        CHECK(plan.ownedTagsToAdd == std::vector<std::string>{"Character.Casting"});
        CHECK(plan.cueIds == std::vector<std::string>{"Cue.Boom"});

        auto res = ActivationPipeline::commit(plan, f.book, /*self*/ 7);

        CHECK(f.book.attributes().currentOf("Mana") == doctest::Approx(70.0f)); // cost paid
        CHECK(f.book.tags().hasTag("Cooldown.Fireball"));   // cooldown effect applied
        CHECK(f.book.tags().hasTag("Character.Casting"));   // owned tag added
        CHECK(f.book.tags().hasTag("Character.Burning"));   // activate effect applied
        CHECK(res.cuesToDispatch == std::vector<std::string>{"Cue.Boom"});
        CHECK(f.book.getCooldownRemaining({"Cooldown.Fireball"}) == doctest::Approx(5.0f));
    }

    TEST_CASE("failure: NotGranted")
    {
        Fixture f;
        buildFireball(f);
        f.wire();
        f.ctx.isGranted = false;

        auto plan = ActivationPipeline::planActivate(f.ctx);
        CHECK_FALSE(plan.valid);
        CHECK(plan.status == ActivationStatus::NotGranted);
    }

    TEST_CASE("failure: BlockedByTags")
    {
        Fixture f;
        buildFireball(f);
        f.book.tags().addTag("Character.Stunned");
        f.wire();

        auto plan = ActivationPipeline::planActivate(f.ctx);
        CHECK_FALSE(plan.valid);
        CHECK(plan.status == ActivationStatus::BlockedByTags);
    }

    TEST_CASE("failure: MissingTags")
    {
        Fixture f;
        buildFireball(f);
        f.book.tags().removeTag("Character.CanCast"); // required tag absent
        f.wire();

        auto plan = ActivationPipeline::planActivate(f.ctx);
        CHECK_FALSE(plan.valid);
        CHECK(plan.status == ActivationStatus::MissingTags);
    }

    TEST_CASE("failure: OnCooldown")
    {
        Fixture f;
        buildFireball(f);
        f.wire();
        f.ctx.cooldownRemaining = 4.0f;

        auto plan = ActivationPipeline::planActivate(f.ctx);
        CHECK_FALSE(plan.valid);
        CHECK(plan.status == ActivationStatus::OnCooldown);
    }

    TEST_CASE("failure: CostNotMet")
    {
        Fixture f;
        buildFireball(f);
        f.book.attributes().setBase("Mana", 10.0f); // not enough for a 30 cost
        f.book.recomputeAttributes();
        f.wire();

        auto plan = ActivationPipeline::planActivate(f.ctx);
        CHECK_FALSE(plan.valid);
        CHECK(plan.status == ActivationStatus::CostNotMet);
    }

    TEST_CASE("failure: NoTarget (and success once a valid target is supplied)")
    {
        Fixture f;
        buildFireball(f);
        f.ability.targeting.type = TargetingType::Single;
        f.ability.targetRequiredTags = {"Team.Enemy"};
        f.wire();

        auto noTarget = ActivationPipeline::planActivate(f.ctx);
        CHECK_FALSE(noTarget.valid);
        CHECK(noTarget.status == ActivationStatus::NoTarget);

        // A target lacking the required tag still fails.
        f.ctx.target.hasTarget = true;
        f.ctx.target.tags.addTag("Team.Player");
        auto wrongTarget = ActivationPipeline::planActivate(f.ctx);
        CHECK(wrongTarget.status == ActivationStatus::NoTarget);

        // Correct target satisfies activation.
        f.ctx.target.tags.addTag("Team.Enemy");
        auto ok = ActivationPipeline::planActivate(f.ctx);
        CHECK(ok.valid);
        CHECK(ok.status == ActivationStatus::Success);
    }

    TEST_CASE("transactional invariant: a failed planActivate + commit mutates nothing")
    {
        Fixture f;
        buildFireball(f);
        f.book.tags().addTag("Character.Stunned"); // will fail BlockedByTags
        f.wire();

        const AttributeState attrsBefore = f.book.attributes();
        const TagContainer tagsBefore = f.book.tags();
        const std::size_t activeBefore = f.book.active().size();

        auto plan = ActivationPipeline::planActivate(f.ctx);
        REQUIRE_FALSE(plan.valid);

        // Committing the empty/invalid plan must be a complete no-op.
        ActivationPipeline::commit(plan, f.book, 1);

        CHECK(f.book.attributes() == attrsBefore);
        CHECK(f.book.tags() == tagsBefore);
        CHECK(f.book.active().size() == activeBefore);
    }

    TEST_CASE("cooldown blocks reactivation and getCooldownRemaining decreases over time")
    {
        Fixture f;
        buildFireball(f);
        f.wire();

        auto plan1 = ActivationPipeline::planActivate(f.ctx);
        REQUIRE(plan1.valid);
        ActivationPipeline::commit(plan1, f.book, 1);

        const float rem = f.book.getCooldownRemaining(f.ability.cooldown.cooldownTags);
        CHECK(rem == doctest::Approx(5.0f));

        // The engine would feed this back into the context for the next attempt.
        f.ctx.cooldownRemaining = rem;
        auto plan2 = ActivationPipeline::planActivate(f.ctx);
        CHECK_FALSE(plan2.valid);
        CHECK(plan2.status == ActivationStatus::OnCooldown);

        f.book.tick(2.0f);
        CHECK(f.book.getCooldownRemaining(f.ability.cooldown.cooldownTags) == doctest::Approx(3.0f));
    }

    TEST_CASE("cost decrements the resource and blocks once it is unaffordable")
    {
        Fixture f;
        f.book.attributes().setAttribute("Mana", attr(100.0f, 0.0f, 1000.0f));
        f.book.recomputeAttributes();
        f.book.tags().addTag("Character.CanCast");

        GameplayEffectSpec cost;
        cost.id = "Cost30";
        cost.durationPolicy = DurationPolicy::Instant;
        cost.modifiers = {mod("Mana", ModifierOp::Add, -30.0f)};
        f.registry[cost.id] = cost;

        f.ability.id = "Zap";
        f.ability.activationRequiredTags = {"Character.CanCast"};
        f.ability.cost.effectId = "Cost30";
        f.ability.targeting.type = TargetingType::Self; // no cooldown => repeatable
        f.wire();

        // Three casts succeed: 100 -> 70 -> 40 -> 10.
        for (int i = 0; i < 3; ++i)
        {
            auto plan = ActivationPipeline::planActivate(f.ctx);
            REQUIRE(plan.valid);
            ActivationPipeline::commit(plan, f.book, 1);
        }
        CHECK(f.book.attributes().currentOf("Mana") == doctest::Approx(10.0f));

        // Fourth cast cannot afford the 30 cost (10 - 30 < 0).
        auto plan4 = ActivationPipeline::planActivate(f.ctx);
        CHECK_FALSE(plan4.valid);
        CHECK(plan4.status == ActivationStatus::CostNotMet);
        CHECK(f.book.attributes().currentOf("Mana") == doctest::Approx(10.0f)); // unchanged
    }
}
