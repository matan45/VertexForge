#pragma once

// Gameplay Ability System (VK-816) — ability spec (parsed .vfAbility).
// ENGINE-FREE (see Tags.hpp header note).

#include <string>
#include <vector>

namespace gas
{
    // How an ability is triggered.
    enum class ActivationPolicy
    {
        OnPressed,
        OnHeld,
        OnGranted,
        Manual
    };

    enum class TargetingType
    {
        Self,
        Single,
        AOE
    };

    // A single linear ability task step. `param` carries the asset/effect/cue id
    // (PlayMontage montage, ApplyEffect effectId, SpawnCue cueId); `delay` is the
    // WaitDelay duration in seconds.
    enum class TaskType
    {
        WaitDelay,
        PlayMontage,
        ApplyEffect,
        SpawnCue
    };

    struct AbilityTask
    {
        TaskType type = TaskType::WaitDelay;
        std::string param;
        float delay = 0.0f;

        bool operator==(const AbilityTask& o) const
        {
            return type == o.type && param == o.param && delay == o.delay;
        }
        bool operator!=(const AbilityTask& o) const { return !(*this == o); }
    };

    // Cost is modeled as an effect (typically Instant negative modifiers).
    struct AbilityCost
    {
        std::string effectId;

        bool operator==(const AbilityCost& o) const { return effectId == o.effectId; }
        bool operator!=(const AbilityCost& o) const { return !(*this == o); }
    };

    // Cooldown is modeled as a Duration effect granting cooldown tags. An explicit
    // effectId takes precedence; otherwise duration + cooldownTags synthesize one.
    struct AbilityCooldown
    {
        float duration = 0.0f;
        std::vector<std::string> cooldownTags;
        std::string effectId;

        bool operator==(const AbilityCooldown& o) const
        {
            return duration == o.duration && cooldownTags == o.cooldownTags &&
                   effectId == o.effectId;
        }
        bool operator!=(const AbilityCooldown& o) const { return !(*this == o); }
    };

    struct AbilityActivation
    {
        std::string inputAction;
        ActivationPolicy policy = ActivationPolicy::OnPressed;

        bool operator==(const AbilityActivation& o) const
        {
            return inputAction == o.inputAction && policy == o.policy;
        }
        bool operator!=(const AbilityActivation& o) const { return !(*this == o); }
    };

    struct AbilityTargeting
    {
        TargetingType type = TargetingType::Self;
        float range = 0.0f;
        float radius = 0.0f;

        bool operator==(const AbilityTargeting& o) const
        {
            return type == o.type && range == o.range && radius == o.radius;
        }
        bool operator!=(const AbilityTargeting& o) const { return !(*this == o); }
    };

    // Parsed form of a .vfAbility asset.
    struct AbilitySpec
    {
        std::string version;
        std::string id;
        std::string displayName;
        std::vector<std::string> abilityTags;
        std::vector<std::string> activationRequiredTags;
        std::vector<std::string> activationBlockedTags;
        std::vector<std::string> activationOwnedTags;  // granted to owner while active
        std::vector<std::string> targetRequiredTags;
        std::vector<std::string> cancelAbilitiesWithTag;
        AbilityCost cost;
        AbilityCooldown cooldown;
        AbilityActivation activation;
        AbilityTargeting targeting;
        std::vector<AbilityTask> tasks;
        std::vector<std::string> effectsOnActivate;
        std::vector<std::string> cueIds;

        bool operator==(const AbilitySpec& o) const
        {
            return version == o.version && id == o.id && displayName == o.displayName &&
                   abilityTags == o.abilityTags &&
                   activationRequiredTags == o.activationRequiredTags &&
                   activationBlockedTags == o.activationBlockedTags &&
                   activationOwnedTags == o.activationOwnedTags &&
                   targetRequiredTags == o.targetRequiredTags &&
                   cancelAbilitiesWithTag == o.cancelAbilitiesWithTag &&
                   cost == o.cost && cooldown == o.cooldown &&
                   activation == o.activation && targeting == o.targeting &&
                   tasks == o.tasks && effectsOnActivate == o.effectsOnActivate &&
                   cueIds == o.cueIds;
        }
        bool operator!=(const AbilitySpec& o) const { return !(*this == o); }
    };
}
