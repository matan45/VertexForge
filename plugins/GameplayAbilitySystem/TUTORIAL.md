# Gameplay Ability System (GAS) — Tutorial

A data-driven ability system (UE5-GAS-inspired, VertexForge-native). You author
**assets** (abilities/effects/cues/tags), put **components** on entities, and
drive everything from **mType scripts** (or bound input actions). All gameplay
state lives in the plugin runtime; scripts talk to it through the
`GameplayAbilitySystem` facade.

---

## 0. Enable the plugin

The plugin must be enabled to register its asset types, components and natives.
Editor: **Plugins → Plugin Manager → enable `GameplayAbilitySystem`** (or set
`"enabled": true` in `plugins/GameplayAbilitySystem/GameplayAbilitySystem.vfplugin`
and restart the editor). It needs the capabilities `editor, scripting, vfx,
audio, input`.

---

## 1. The four assets

Create them in the **Content Browser → right-click → Create → Gameplay Ability
System**, then **double-click** to open the matching editor. Paths below are
**project-relative** (e.g. `assets/gas/...`).

### `.vfGameplayTags` — the project tag dictionary
A flat list of hierarchical **dotted** tags (`Character`, `Character.Stunned`,
`Cooldown.Fireball`). Hierarchy is by the dots: a query for `Character` is
satisfied by an owned `Character.Stunned` (parent-query matches descendants),
but owning `Character` does **not** satisfy a query for `Character.Stunned`.
Tags are how abilities gate themselves (required/blocked) and how effects mark
state and cooldowns. You can have several tag tables; they merge at load.

> Tags do not *have* to be declared here to be used at runtime (the runtime
> treats them as strings) — the table is for authoring/validation/autocomplete.

### `.vfGameplayEffect` — a package of attribute changes + tags
The verb of the system. Fields:
- **durationPolicy**: `Instant` (apply once to the attribute **base**, permanent),
  `Duration` (lasts `duration` seconds, contributes while active, reverts on
  expiry), `Infinite` (until removed).
- **duration / period**: for Duration/Infinite. `period > 0` makes it **periodic** —
  it fires its modifiers to the base every `period` seconds (a tick of damage/heal).
- **modifiers**: `[{attribute, op, magnitude}]` where `op` is `Add`, `Multiply`
  (1.5 = ×1.5, a +50% buff), or `Override` (sets the value, wins over add/mul).
- **grantedTags**: tags the owner has while the effect is active (e.g. a cooldown
  effect grants `Cooldown.Fireball`; a stun grants `Character.Stunned`).
- **ongoingRequiredTags / removalTags**: (advanced) gating / auto-removal hooks.
- **stacking**: `{policy: None|BySource|ByTarget, limit, durationRefresh, expiration}`.
- **cueIds**: cues to fire when this effect is applied (cosmetic).

### `.vfGameplayCue` — cosmetic feedback (VFX + audio)
Fired by effects/abilities; never changes gameplay state. Fields: `id`,
`trigger` (`OnApply|OnRemove|OnActive|OnExecute`), `vfxPath` (a `.vfVFX`),
`attachSocket`, `audioPath` (a `.vfAudio`), `params`. The runtime spawns the VFX
(fire-and-forget) and plays the audio at the owner's world position.

### `.vfAbility` — an activatable ability
The thing a unit *does*. Tabs in the editor:
- **Activation**: `id` (the name you activate by — e.g. `Fireball`),
  `displayName`, `activation.inputAction` (bound input action name),
  `activation.policy` (`OnPressed|OnHeld|OnGranted|Manual`),
  `targeting.type` (`Self|Single|AOE`), `range`, `radius`.
- **Requirements**: tag gates — `activationRequiredTags` (owner must have all),
  `activationBlockedTags` (owner must have none), `activationOwnedTags` (granted
  while active), `targetRequiredTags`, `cancelAbilitiesWithTag`.
- **Tasks**: a simple linear list (`WaitDelay | PlayMontage | ApplyEffect |
  SpawnCue`) run on activation.
- **Effects**: `cost.effectId` (an effect path, usually `Instant` negative
  modifiers — e.g. −25 Mana), `cooldown` (`duration` + `cooldownTags`, or an
  effect), `effectsOnActivate` (effect paths applied to the target).
- **Cues**: cue paths fired on activation.

> **id vs path:** you **grant** an ability by its **asset path**; you **activate**
> it by its **`id`** (the `id` field). Effects/cues are referenced by **path**.

---

## 2. The four components (Add Component → `GAS_*`)

| Component | Purpose | Authored fields | Runtime mirrors (read-only) |
|---|---|---|---|
| `GAS_AbilitySystem` | abilities the entity owns | `grantedAbilities` (`.vfAbility` refs), `pollInput` | — |
| `GAS_AttributeSet` | named attributes | `attributeNames`, `baseValues`, `minValues`, `maxValues` (parallel arrays) | `currentValues` |
| `GAS_Tags` | gameplay tags | `looseTags` | `grantedTags` |
| `GAS_ActiveEffects` | effects applied on seed | `startupEffects` (`.vfGameplayEffect` refs) | `activeEffectSummary` |

On **Play**, every entity carrying any GAS component is **seeded**: attributes
loaded, loose tags added, granted abilities registered, startup effects applied.
The `*_runtime` mirror fields update each frame so you can watch state in the
inspector. GAS only runs **in Play mode** — it's dormant in the editor.

Minimum to make a unit usable: `GAS_AttributeSet` (e.g. `[Health, Mana]` with
base `[60,100]`, min `[0,0]`, max `[100,100]`) + `GAS_AbilitySystem` with an
ability in `grantedAbilities`.

---

## 3. Two ways to activate

**A) Bound input (no script).** Set the ability's `activation.policy = OnPressed`
and `inputAction = "Fireball"`, keep `GAS_AbilitySystem.pollInput = true`, and map
a key to the `Fireball` input action. GAS auto-activates it each time the action
is pressed. This passes **no target**, so it only works for **Self** abilities
(targeted abilities need a target — use a script).

**B) From mType.** Full control, including targets. See below.

---

## 4. Using it from mType

Import the facade (note the `plugin/` subfolder):

```mtype
import * from "engine/Entity.mt";
import * from "engine/plugin/GameplayAbilitySystem.mt";
```

### Facade API

| Method | Returns | Notes |
|---|---|---|
| `grantAbility(entityId, abilityPath)` | `int` (1/0) | by **path** |
| `revokeAbility(entityId, abilityId)` | `int` | by **id** |
| `canActivate(entityId, abilityId, targetId)` | `int` status | no mutation |
| `canActivateSelf(entityId, abilityId)` | `int` status | |
| `activate(entityId, abilityId, targetId)` | `int` status | transactional |
| `activateSelf(entityId, abilityId)` | `int` status | Self abilities |
| `cancel(entityId, handle)` | `void` | |
| `getCooldownRemaining(entityId, abilityId)` | `float` | seconds |
| `applyEffect(sourceId, targetId, effectPath)` | `int` handle | by **path**; 0 = Instant/fail |
| `removeEffect(targetId, handle)` | `bool` | |
| `getAttribute(entityId, name)` | `float` | aggregated current |
| `getAttributeBase(entityId, name)` | `float` | base |
| `hasTag(entityId, tag)` | `bool` | hierarchical |
| `hasAllTags(entityId, tags)` | `bool` | `string[]` |
| `hasAnyTag(entityId, tags)` | `bool` | `string[]` |
| `getTags(entityId)` | `string[]` | all current tags |
| `isSuccess(status)` / `noTarget()` | `bool` / `int` | helpers |

**Activation status codes** (returned by `canActivate`/`activate`):
`0 Success, 1 InvalidEntity, 2 NotGranted, 3 BlockedByTags, 4 MissingTags,
5 OnCooldown, 6 CostNotMet, 7 NoTarget, 8 Internal`.

The natives never destroy entities — death/cleanup is your script's decision
(react to attribute values or the `gas.*` C++ events). Scripts **poll** state
(the `gas.*` events are for C++ subscribers like the Debugger, not scripts).

### Example: a self-buff caster

```mtype
import * from "engine/Entity.mt";
import * from "engine/Input.mt";
import * from "engine/Key.mt";
import * from "engine/plugin/GameplayAbilitySystem.mt";

@Script
public class EmpowerCaster {
    private int self;

    public function onStart(): void {
        this.self = Entity::self();
        // Grant by PATH (project-relative).
        GameplayAbilitySystem::grantAbility(this.self, "assets/gas/abilities/Empower.vfAbility");
    }

    public function onUpdate(float deltaTime): void {
        // Activate by ID when E is pressed and it's ready.
        if (Input::isKeyPressed(Key::E())) {
            int status = GameplayAbilitySystem::activateSelf(this.self, "Empower");
            if (!GameplayAbilitySystem::isSuccess(status)) {
                // status 5 = OnCooldown, 6 = CostNotMet, ...
            }
        }
    }
}
```

### Example: a targeted ability + reading state

```mtype
// Cast Fireball at another unit, then react to its health.
int status = GameplayAbilitySystem::activate(this.self, "Fireball", enemyId);
if (GameplayAbilitySystem::isSuccess(status)) {
    float enemyHp = GameplayAbilitySystem::getAttribute(enemyId, "Health");
    if (enemyHp <= 0.0) {
        // GAS won't kill it for you — do cleanup here.
        Entity::setActive(enemyId, false);
    }
}

// Gate behaviour on tags / cooldown.
if (GameplayAbilitySystem::hasTag(this.self, "Character.Stunned")) { return; }
float cd = GameplayAbilitySystem::getCooldownRemaining(this.self, "Fireball");

// Apply an effect directly (e.g. a buff), by PATH.
int handle = GameplayAbilitySystem::applyEffect(this.self, allyId, "assets/gas/effects/Shield.vfGameplayEffect");
```

---

## 5. How a cast resolves (mental model)

`activate(owner, abilityId, target)` is **transactional** — it validates
everything first and only then commits, in this order:

1. ability is **granted** → else `NotGranted`
2. owner has none of `activationBlockedTags` → else `BlockedByTags`
3. owner has all `activationRequiredTags` → else `MissingTags`
4. not on **cooldown** → else `OnCooldown`
5. **cost** affordable (dry-run the cost effect vs current attributes) → else `CostNotMet`
6. **target** valid for non-Self targeting → else `NoTarget`

On success it applies: cost → cooldown → `effectsOnActivate` → owned tags → tasks
→ cues, and emits `gas.ability_activated` + `gas.effect_applied` /
`gas.attribute_changed`. A failure mutates **nothing**.

Attributes aggregate as `current = clamp((base + ΣAdd) × ΠMultiply, min, max)`
(Override wins). Watch it all live in **Plugins → GAS Debugger** while playing.

---

## 6. Gotchas

- **Play only.** GAS is dormant in edit mode; effects/cooldowns advance on the
  game clock (respect pause + time-scale). At time-scale `0` nothing advances.
- **Paths are project-relative** (`assets/gas/...`) and resolved via the engine,
  so they work from any working directory. Grant/effect/cue references use paths;
  activation uses the ability **id**.
- **Attribute visible change:** a heal won't show if the attribute is already at
  its max (it clamps) — start below max to see it.
- **Cues are cosmetic** and need a valid `.vfVFX` / `.vfAudio` path; a missing one
  is skipped (the ability still works).
