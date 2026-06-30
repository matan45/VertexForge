#pragma once

#include "asset/AssetRef.hpp"
#include <cstdint>
#include <string>
#include <vector>

// Gameplay Ability System components (VK-816). Registered as plugin native
// components (registerNativeComponent) so the engine core stays game-agnostic.
//
// IMPORTANT: these carry ONLY authored data plus a few read-only mirrors the
// runtime refreshes each tick for the inspector. All authoritative transient
// state — live attribute currents, active effect instances, cooldown timers,
// granted-ability and activation state — lives in GASRuntime side-stores keyed
// by entity and is NEVER serialized. Field types are kept flat
// (vector<string>/vector<float>/AssetRef/bool) for MetaJsonSerializer support.

// The ability system component: which abilities the entity has been granted and
// whether the runtime should poll input actions to auto-activate them.
struct GAS_AbilitySystemComponent
{
    std::vector<asset::AssetRef> grantedAbilities; // .vfAbility refs
    bool pollInput = true;
    std::int64_t targetEntity = -1; // transient runtime target, intentionally not reflected/serialized
};

// Named attributes as parallel arrays. currentValues is a READ-ONLY mirror the
// runtime refreshes from the aggregator each tick (the authoritative current
// value lives in the GASRuntime side-store; editing this field does nothing).
struct GAS_AttributeSetComponent
{
    std::vector<std::string> attributeNames;
    std::vector<float> baseValues;
    std::vector<float> minValues;
    std::vector<float> maxValues;
    std::vector<float> currentValues; // read-only mirror
};

// Loose (authored) tags plus a read-only mirror of the effect/ability-granted
// tags the runtime layers on top each tick.
struct GAS_TagComponent
{
    std::vector<std::string> looseTags;   // authored
    std::vector<std::string> grantedTags; // read-only mirror
};

// Effects applied to the entity when it is first seeded, plus a read-only
// summary mirror of the currently-active effects for the inspector/debugger.
struct GAS_ActiveEffectsComponent
{
    std::vector<asset::AssetRef> startupEffects;  // .vfGameplayEffect refs
    std::vector<std::string> activeEffectSummary; // read-only mirror
};
