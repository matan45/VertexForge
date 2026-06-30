#include <doctest.h>

// VK-816 (Gameplay Ability System) — asset (de)serialization: full struct-equality
// roundtrips for all four asset types (incl. nested modifiers/tasks/stacking/params),
// valid createDefault output, tolerant parsing of minimal JSON, and file save/load.

#include "../../plugins/GameplayAbilitySystem/gas/core/GASAssets.hpp"

#include <filesystem>
#include <string>

using namespace gas;

namespace
{
    GameplayEffectSpec fullEffect()
    {
        GameplayEffectSpec s;
        s.version = "1.0";
        s.id = "Effect.Poison";
        s.displayName = "Poison";
        s.durationPolicy = DurationPolicy::Duration;
        s.duration = 4.0f;
        s.period = 0.5f;
        s.modifiers = {
            {"Health", ModifierOp::Add, -5.0f, 0},
            {"Speed", ModifierOp::Multiply, 0.5f, 0},
            {"Armor", ModifierOp::Override, 2.0f, 0}};
        s.grantedTags = {"Character.Poisoned", "State.Debuff"};
        s.ongoingRequiredTags = {"Character.Alive"};
        s.removalTags = {"Cure.Antidote"};
        s.stacking.policy = StackingPolicy::BySource;
        s.stacking.limit = 3;
        s.stacking.durationRefresh = true;
        s.stacking.expiration = StackExpiration::RemoveSingle;
        s.cueIds = {"Cue.PoisonTick"};
        return s;
    }

    AbilitySpec fullAbility()
    {
        AbilitySpec s;
        s.version = "1.0";
        s.id = "Ability.Fireball";
        s.displayName = "Fireball";
        s.abilityTags = {"Ability.Fire", "Ability.Offensive"};
        s.activationRequiredTags = {"Character.CanCast"};
        s.activationBlockedTags = {"Character.Stunned", "Character.Silenced"};
        s.activationOwnedTags = {"Character.Casting"};
        s.targetRequiredTags = {"Team.Enemy"};
        s.cancelAbilitiesWithTag = {"Ability.Channeled"};
        s.cost.effectId = "Cost.Mana30";
        s.cooldown.duration = 8.0f;
        s.cooldown.cooldownTags = {"Cooldown.Fireball"};
        s.cooldown.effectId = "";
        s.activation.inputAction = "Ability1";
        s.activation.policy = ActivationPolicy::OnHeld;
        s.targeting.type = TargetingType::AOE;
        s.targeting.range = 12.0f;
        s.targeting.radius = 4.0f;
        s.tasks = {
            {TaskType::WaitDelay, "", 0.5f},
            {TaskType::PlayMontage, "montage.cast", 0.0f},
            {TaskType::ApplyEffect, "Effect.Burn", 0.0f},
            {TaskType::SpawnCue, "Cue.Boom", 0.0f}};
        s.effectsOnActivate = {"Effect.Burn"};
        s.cueIds = {"Cue.Boom", "Cue.Cast"};
        return s;
    }

    GameplayCueSpec fullCue()
    {
        GameplayCueSpec s;
        s.version = "1.0";
        s.id = "Cue.Boom";
        s.trigger = CueTrigger::OnExecute;
        s.vfxPath = "vfx/explosion_fireball.vfVFX";
        s.attachSocket = "hand_r";
        s.audioPath = "audio/boom.vfAudio";
        s.params = {{"scale", 2.0f}, {"intensity", 0.5f}};
        return s;
    }

    std::filesystem::path tempFile(const char* name)
    {
        return std::filesystem::temp_directory_path() / name;
    }
}

TEST_SUITE("GAS.Assets")
{
    TEST_CASE("GameplayEffect roundtrips through JSON with struct equality")
    {
        const auto src = fullEffect();
        const auto parsed = EffectAsset::fromJson(EffectAsset::toJson(src));
        CHECK(parsed == src);
    }

    TEST_CASE("Ability roundtrips through JSON with struct equality (nested tasks/cost/cooldown)")
    {
        const auto src = fullAbility();
        const auto parsed = AbilityAsset::fromJson(AbilityAsset::toJson(src));
        CHECK(parsed == src);
    }

    TEST_CASE("GameplayCue roundtrips through JSON with struct equality (params map)")
    {
        const auto src = fullCue();
        const auto parsed = CueAsset::fromJson(CueAsset::toJson(src));
        CHECK(parsed == src);
    }

    TEST_CASE("GameplayTags table roundtrips through JSON")
    {
        TagTable src;
        src.declare("Character", "root");
        src.declare("Character.Stunned", "cannot act");
        src.declare("Ability.Fireball");
        const auto parsed = TagsAsset::fromJson(TagsAsset::toJson(src));
        CHECK(parsed == src);
    }

    TEST_CASE("createDefault produces a valid, roundtrippable spec for each type")
    {
        auto effect = EffectAsset::createDefault("MyEffect");
        CHECK(effect.id == "MyEffect");
        CHECK(effect.version == GAS_FORMAT_VERSION);
        CHECK(EffectAsset::fromJson(EffectAsset::toJson(effect)) == effect);

        auto ability = AbilityAsset::createDefault("MyAbility");
        CHECK(ability.id == "MyAbility");
        CHECK(AbilityAsset::fromJson(AbilityAsset::toJson(ability)) == ability);

        auto cue = CueAsset::createDefault("MyCue");
        CHECK(cue.id == "MyCue");
        CHECK(CueAsset::fromJson(CueAsset::toJson(cue)) == cue);

        auto tags = TagsAsset::createDefault();
        CHECK_FALSE(tags.empty());
        CHECK(tags.contains("Character.Stunned"));
        CHECK(TagsAsset::fromJson(TagsAsset::toJson(tags)) == tags);
    }

    TEST_CASE("tolerant parsing: minimal JSON fills defaults")
    {
        nlohmann::json minimalEffect = {{"id", "Min"}};
        auto e = EffectAsset::fromJson(minimalEffect);
        CHECK(e.id == "Min");
        CHECK(e.durationPolicy == DurationPolicy::Instant); // default
        CHECK(e.modifiers.empty());
        CHECK(e.stacking.policy == StackingPolicy::None);
        CHECK(e.stacking.limit == 1);

        nlohmann::json minimalAbility = {{"id", "A"}};
        auto a = AbilityAsset::fromJson(minimalAbility);
        CHECK(a.id == "A");
        CHECK(a.targeting.type == TargetingType::Self);          // default
        CHECK(a.activation.policy == ActivationPolicy::OnPressed); // default
        CHECK(a.tasks.empty());

        // Unknown enum string falls back to the type default.
        nlohmann::json weird = {{"id", "W"}, {"durationPolicy", "Bogus"}};
        auto w = EffectAsset::fromJson(weird);
        CHECK(w.durationPolicy == DurationPolicy::Instant);

        // A plain-string tag entry is accepted alongside object entries.
        nlohmann::json tagJson = {{"tags", {"Character", nlohmann::json{{"tag", "Ability.Fire"}, {"comment", "c"}}}}};
        auto t = TagsAsset::fromJson(tagJson);
        CHECK(t.contains("Character"));
        CHECK(t.contains("Ability.Fire"));
    }

    TEST_CASE("save then load reproduces each spec from disk")
    {
        {
            const auto src = fullEffect();
            const auto path = tempFile("gas_effect_test.vfGameplayEffect");
            REQUIRE(EffectAsset::save(path.string(), src));
            auto loaded = EffectAsset::load(path.string());
            REQUIRE(loaded.has_value());
            CHECK(*loaded == src);
            std::filesystem::remove(path);
        }
        {
            const auto src = fullAbility();
            const auto path = tempFile("gas_ability_test.vfAbility");
            REQUIRE(AbilityAsset::save(path.string(), src));
            auto loaded = AbilityAsset::load(path.string());
            REQUIRE(loaded.has_value());
            CHECK(*loaded == src);
            std::filesystem::remove(path);
        }
        {
            const auto src = fullCue();
            const auto path = tempFile("gas_cue_test.vfGameplayCue");
            REQUIRE(CueAsset::save(path.string(), src));
            auto loaded = CueAsset::load(path.string());
            REQUIRE(loaded.has_value());
            CHECK(*loaded == src);
            std::filesystem::remove(path);
        }
        {
            TagTable src = TagsAsset::createDefault();
            const auto path = tempFile("gas_tags_test.vfGameplayTags");
            REQUIRE(TagsAsset::save(path.string(), src));
            auto loaded = TagsAsset::load(path.string());
            REQUIRE(loaded.has_value());
            CHECK(*loaded == src);
            std::filesystem::remove(path);
        }
    }

    TEST_CASE("load of a missing file returns nullopt")
    {
        CHECK_FALSE(AbilityAsset::load("Z:/definitely/not/here.vfAbility").has_value());
    }
}
