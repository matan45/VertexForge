# VFX Starter Library

Ready-to-use `.vfVFX` effects for the RTS demo. Open any of them in the VFX Editor
to tweak, or reference them from a `VFXComponent` / spawn from mType:

```mt
// One-shot at a world position (auto-destroys when finished)
VFX::spawnAt("assets/vfx/library/explosion_fireball.vfVFX", hit.x, hit.y, hit.z);

// Looping, caller-owned
int fireId = VFX::spawnAtLooping("assets/vfx/library/fire_loop.vfVFX", x, y, z);
VFX::destroyInstance(fireId);

// Team tint
VFX::setOverrideColor(id, 0.2, 0.4, 1.0, 1.0);
```

| Effect | Type | Notes |
|---|---|---|
| explosion_fireball | one-shot burst | 120-particle pop, expands + fades, emits light |
| muzzle_flash | one-shot burst | 0.08s flash, emits light; attach to a weapon socket |
| smoke_column | looping | turbulence + wind, lit, soft particles |
| fire_loop | looping | additive flames, emits flickering-radius light |
| dust_impact | one-shot burst | projectile/footstep impact puff, lit |
| rain_splash | one-shot burst | small splash crown, gravity |
| sparks | one-shot burst | stretched billboards, scene collision + bounce |
| tracer_trail | looping ribbon | attach to a moving projectile entity |
| building_collapse_dust | one-shot 2-burst | wide box volume, heavy lingering dust |
| magic_heal | looping | vortex swirl, additive green, emits light |
| ground_fog | looping | large soft lit quads drifting with wind |

All effects ship with no texture assigned (engine default white particle).
Assign a `.vfImage` in the emitter's `texture` property for full quality —
only `.vfImage` is supported by the engine loaders.

## Combo sequences (`.vfVFXSequence` — VK-1425)

A `.vfVFXSequence` composes several of the `.vfVFX` effects above into one timed
"combo" (no graph duplication — each step just references an existing `.vfVFX`).
Open one in the **VFX Sequence Editor** (double-click), or drive it at runtime.

| Example | Shows |
|---|---|
| `example_impact_combo.vfVFXSequence` | 4 one-shot steps at staggered times (muzzle → sparks → dust → smoke) — plays standalone |
| `example_cast_combo.vfVFXSequence` | a **looping** charge effect on the `hand_R` socket + a **cue-driven** release step (`cueName: "release"`) |

```mt
// Standalone: spawn + play a whole combo at a world position
int combo = VFX::spawnCombo("assets/vfx/library/example_impact_combo.vfVFXSequence", x, y, z);

// Cast combo: looping charge follows the hand; fire the release cue on attack
int cast = VFX::spawnComboLooping("assets/vfx/library/example_cast_combo.vfVFXSequence", x, y, z);
VFX::attachComboToSocket(cast, casterEntity, "hand_R");
VFX::triggerComboCue(cast, "release");
VFX::destroyCombo(cast);
```

Data-driven alternative: add a **VFX Sequence** component to an entity, set
`sequenceRef` + `autoPlay` for standalone play, or add a **trigger**
(`eventName → sequence`) so an authored animation notify event spawns the combo
automatically (see VK-1425). The referenced child `.vfVFX` here use project paths,
so they resolve before GUIDs are assigned; re-saving from the editor bakes GUID
references + a `.vfmeta` dependency list.
