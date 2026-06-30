#pragma once

// Gameplay Ability System (VK-816) — gameplay effect spec + runtime instance type.
// ENGINE-FREE (see Tags.hpp header note).

#include "Attributes.hpp"

#include <string>
#include <vector>
#include <cstdint>

namespace gas
{
    // How long an effect lives.
    //   Instant  -> applied once to BASE, then discarded (not stored).
    //   Duration -> active for `duration` seconds, then reverts.
    //   Infinite -> active until explicitly removed.
    enum class DurationPolicy
    {
        Instant,
        Duration,
        Infinite
    };

    // How repeated applications combine.
    //   None     -> does not stack; capped at `limit` separate instances of the
    //               spec, applications beyond the limit are rejected.
    //   BySource -> one stack per (spec, source); same-source reapplication
    //               increments stackCount up to `limit`.
    //   ByTarget -> one stack per spec on this target (source ignored); any
    //               reapplication increments stackCount up to `limit`.
    enum class StackingPolicy
    {
        None,
        BySource,
        ByTarget
    };

    // What an expiry does to a multi-stack effect.
    //   ClearStack   -> the whole effect (all stacks) is removed.
    //   RemoveSingle -> one stack is removed and the duration refreshed; the
    //                   effect persists until the last stack expires.
    enum class StackExpiration
    {
        ClearStack,
        RemoveSingle
    };

    struct StackingConfig
    {
        StackingPolicy policy = StackingPolicy::None;
        int limit = 1;
        bool durationRefresh = false; // reapplication resets timeRemaining
        StackExpiration expiration = StackExpiration::ClearStack;

        bool operator==(const StackingConfig& o) const
        {
            return policy == o.policy && limit == o.limit &&
                   durationRefresh == o.durationRefresh && expiration == o.expiration;
        }
        bool operator!=(const StackingConfig& o) const { return !(*this == o); }
    };

    // Parsed form of a .vfGameplayEffect asset.
    //
    // Periodic semantics: when period > 0 the effect's modifiers do NOT contribute
    // to continuous aggregation; instead they fire once per `period` (like an
    // Instant execution against BASE) and are NOT reverted on expiry. When
    // period == 0 the modifiers contribute continuously to aggregation and ARE
    // reverted when the effect expires/removes. (Mirrors UE.)
    struct GameplayEffectSpec
    {
        std::string version;
        std::string id;
        std::string displayName;
        DurationPolicy durationPolicy = DurationPolicy::Instant;
        float duration = 0.0f; // seconds, for Duration
        float period = 0.0f;   // seconds, 0 => not periodic
        std::vector<AttributeModifier> modifiers;
        std::vector<std::string> grantedTags;
        std::vector<std::string> ongoingRequiredTags;
        std::vector<std::string> removalTags;
        StackingConfig stacking;
        std::vector<std::string> cueIds;

        bool operator==(const GameplayEffectSpec& o) const
        {
            return version == o.version && id == o.id && displayName == o.displayName &&
                   durationPolicy == o.durationPolicy && duration == o.duration &&
                   period == o.period && modifiers == o.modifiers &&
                   grantedTags == o.grantedTags && ongoingRequiredTags == o.ongoingRequiredTags &&
                   removalTags == o.removalTags && stacking == o.stacking && cueIds == o.cueIds;
        }
        bool operator!=(const GameplayEffectSpec& o) const { return !(*this == o); }
    };

    // A live effect on one owner (a stored copy of its spec keeps the book
    // self-contained — no dangling pointers into the resolver/asset registry).
    struct ActiveEffect
    {
        std::uint64_t handle = 0;
        std::string specId;
        GameplayEffectSpec spec;
        float timeRemaining = 0.0f;    // Duration only; ignored for Infinite
        float periodAccumulator = 0.0f;
        int stackCount = 1;
        std::uint64_t sourceId = 0;
        bool inhibited = false;        // ongoingRequiredTags not met (suppresses continuous mods)
    };

    // Reported per periodic fire so the engine can dispatch cues/feedback.
    struct PeriodicFire
    {
        std::uint64_t handle = 0;
        std::string specId;
        int stackCount = 1;
    };
}
