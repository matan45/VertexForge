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
