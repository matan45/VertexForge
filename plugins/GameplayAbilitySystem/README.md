# Gameplay Ability System (GAS) — VK-816

A UE5-GAS-inspired, VertexForge-native ability system, delivered as a plugin. Built on the
generic plugin asset-type SDK (VK-1449).

## What it provides

- **Components** (Add Component → `GAS_*`): `GAS_AbilitySystem` (granted abilities + input
  poll), `GAS_AttributeSet` (named attributes base/min/max + runtime current mirror),
  `GAS_Tags` (loose + granted), `GAS_ActiveEffects` (startup effects + runtime summary).
- **Data assets** (Content Browser → right-click → Create → *Gameplay Ability System*):
  `.vfAbility`, `.vfGameplayEffect`, `.vfGameplayCue`, `.vfGameplayTags`.
- **Editor windows** (Plugins menu): GAS Ability Editor, GAS Effect Editor, GAS Tag Table,
  GAS Debugger (live `gas.*` event log + per-entity attributes). Double-clicking a GAS asset
  opens the matching editor.
- **mType facade**: `assets/scripts/lib/engine/plugin/GameplayAbilitySystem.mt` —
  `import * from "engine/plugin/GameplayAbilitySystem.mt";` then
  `GameplayAbilitySystem::grantAbility/activate/getAttribute/hasTag/getCooldownRemaining/...`.
- **Plugin events** (C++ subscribers): `gas.ability_activated|ability_failed|effect_applied|
  effect_removed|attribute_changed|tag_added|tag_removed`.

## Architecture

- `gas/core/` — **engine-free** (std + nlohmann/json only) pure logic: tags, attributes +
  aggregator, effects (instant/duration/periodic/stacking), transactional activation,
  asset (de)serialization. Unit-tested in `VFEngine/tests/test_gas_*.cpp`.
- `gas/runtime/GASRuntime` — engine glue: per-entity `EffectBook` side-stores, asset caches,
  the `_gas_*` native operations, the per-frame tick (effects + input poll), cue dispatch.
- `gas/components/`, `gas/editor/` — ECS components and the authoring windows.

## Demo set (`assets/gas/`)

`Fireball.vfAbility` (cost `ManaCost`, 3s cooldown, applies `FireballDamage`, single target,
`Fireball` input action) + `FireballDamage`/`ManaCost` effects + `FireballImpact` cue +
`ProjectTags`. To try it: add `GAS_AbilitySystem` + `GAS_AttributeSet` (Health, Mana) to an
entity, either drag `Fireball.vfAbility` into *Granted Abilities* or call
`GameplayAbilitySystem::grantAbility(self, "gas/abilities/Fireball.vfAbility")`, then activate
from script / the bound input action in Play.

## v1 limitations / follow-ups

- Runtime asset loading uses raw file I/O (`gas/core`), resolving paths relative to the
  editor/runtime CWD (an `assets/` fallback is tried). Proper VFS / `AssetRef`-GUID reference
  resolution is a follow-up.
- `gas.*` events are C++-only (not script-subscribable); scripts **poll** the facade.
- Ability cancel is a no-op (no long-running ability instances are tracked in v1).
- In editor Play, GAS uses the scaled game delta and self-gates on it; precise per-frame
  pause/time-scale is inherited from the engine's game-time tick.
