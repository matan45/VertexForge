#include "MTypeApiDoc.hpp"
#include "PathSandbox.hpp"
#include "../protocol/ArgReader.hpp"
#include "../protocol/ResourceRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <system_error>
#include <utility>

namespace mcp::tools::mtypedoc
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr std::uintmax_t maxModuleBytes = 1024 * 1024;

        // ------------------------------------------------------------------
        // Embedded primer. Every rule was checked against the mType compiler
        // (dependencies/mType: lexer keyword table, @Script / @Override validators,
        // import resolution, builtin natives), the engine script host
        // (ScriptingAdapterLifecycle) and the shipped scripts under
        // assets/scripts. Split into raw-string chunks well below MSVC's
        // per-literal limit.
        // ------------------------------------------------------------------

        constexpr const char* primerOverview = R"MTDOC(# VertexForge mType scripting primer

Read this whole page before writing a script. The rules below come from the mType compiler, the engine's script host and the scripts that ship with the engine; breaking one usually fails `scripts_build`.

## 1. Files and folders

- The scripts root is `<project asset root>/scripts`. Every `script_*` tool path and every `vf://scripts/...` URI is relative to it.
- `game/**/*.mt` are the game scripts: the only files `scripts_build` compiles (scripts.mtproj `<Include>game/**/*.mt</Include>`) and the only files `script_attach` accepts. Create them with `script_write`, e.g. path `game/WasdMover.mt`.
- `lib/**/*.mt` is the engine scripting API and is read-only. Read one module's signatures with `vf://docs/mtype-api/<module>` (e.g. `vf://docs/mtype-api/engine/Physics`) or its commented source with `script_read` (path `lib/engine/Physics.mt`).

## 2. Imports are relative to the importing file

`import * from "<path>";` resolves `<path>` first against the folder of the file that contains the import. Use forward slashes only: a backslash is a string escape and can corrupt the path.

| Your file | Import of lib/engine/Input.mt |
|---|---|
| `scripts/game/Player.mt` | `import * from "../lib/engine/Input.mt";` |
| `scripts/game/enemies/Grunt.mt` | `import * from "../../lib/engine/Input.mt";` |
| `scripts/game/a/b/Boss.mt` | `import * from "../../../lib/engine/Input.mt";` |

Game scripts import each other the same way: from `game/enemies/Grunt.mt`, `import * from "../Health.mt";` reaches `game/Health.mt`; a file in the same folder is `import * from "Health.mt";`.

- Import the module of every lib class you name in your code (e.g. `Input` and `Key` for the keyboard, `Vec3f` for vectors).
- Avoid circular imports (A imports B and B imports A, directly or through other files): break the cycle instead of importing back.

## 3. One namespace per build

`scripts_build` compiles every `game/**/*.mt` file plus everything they import as ONE bundle, so every class name must be unique across that whole set.

- Give game classes distinctive names (`WasdMover`, `EnemySpawner`), never the name of a lib class. Importing `lib/engine/oop/Behaviour.mt` alone pulls in `Behaviour`, `GameObject`, `Transform`, `Entity`, `Log`, `RigidBody`, `Collider`, `CameraComponent`, `AnimatorComponent`, `Animator`, `AudioSource`, `NavAgent`, `MeshRenderer`, `UIElement`, `Vec3f` and `Quaternion`, among others.
- Check the names already taken: `resources/list` shows the project's game scripts as `vf://scripts/game/...`.
)MTDOC";

        constexpr const char* primerBehaviour = R"MTDOC(
## 4. A behaviour script

Verified example: `scripts/game/examples/Projectile.mt` ships with the engine and compiles (quoted verbatim; it lives in `game/examples/`, hence `../../lib`). It shows the Behaviour structure, `this.` fields, a component accessor and a listener interface.

```mtype
// Projectile - Example of physics + listener callbacks (VK-1458 OOP style).
//
// Listener interfaces stay interfaces (the engine's dispatch gate keys on
// the implements clause); inside the callback the other entity is wrapped
// with GameObject::fromId for object-style access.

import * from "../../lib/engine/oop/Behaviour.mt";
import * from "../../lib/engine/oop/GameObject.mt";
import * from "../../lib/engine/oop/RigidBody.mt";
import * from "../../lib/engine/ICollisionListener.mt";

@Script
public class Projectile extends Behaviour implements ICollisionListener {
    private float launchSpeed = 30.0;
    private float lifetime = 5.0;
    private float age = 0.0;

    public constructor() : super() {
    }

    @Override
    public function onStart(): void {
        RigidBody body = this.gameObject().rigidBody();
        if (body.exists()) {
            body.applyImpulse(this.transform().forward().multiply(this.launchSpeed));
        } else {
            this.logWarn("Projectile has no rigid body - add one to the prefab");
        }
    }

    @Override
    public function onUpdate(float deltaTime): void {
        this.age = this.age + deltaTime;
        if (this.age > this.lifetime) {
            this.destroySelf();
        }
    }

    @Override
    public function onCollisionEnter(int otherEntityId): void {
        GameObject other = GameObject::fromId(otherEntityId);
        this.log("hit " + other.name());
        this.destroySelf();
    }

    @Override
    public function onCollisionExit(int otherEntityId): void {
    }
}
```

Rules checked by the `@Script` validator and the script host:

- Annotate the class `@Script`. It must be concrete (not `abstract`), have a constructor with no parameters, and have `onStart(): void`, `onUpdate(float deltaTime): void` and `onDestroy(): void`, declared or inherited.
- Extend `Behaviour` (lib/engine/oop/Behaviour.mt). It supplies empty `onStart` / `onUpdate` / `onDestroy`, so override only what you need. Its constructor takes no arguments; write `public constructor() : super() { }`.
- Put `@Override` on each method that overrides a `Behaviour` method (`onStart`, `onUpdate`, `onDestroy`) or implements an interface method (e.g. `onCollisionEnter` of `ICollisionListener`). A missing `@Override` is only a warning, but `@Override` on a method that overrides nothing is a compile error. Keep the exact parameter types.
- Optional hooks the engine calls when present: `public function onLateUpdate(float deltaTime): void`, `public function onFixedUpdate(float fixedDeltaTime): void`, `public function onEnable(): void`, `public function onDisable(): void`. `Behaviour` does NOT declare them, so do NOT put `@Override` on them.
- Inside a `Behaviour`:
  - `this.transform()` returns the entity's `Transform`: `localPosition()` / `setLocalPosition(v)`, `translate(delta)`, `rotate(eulerDegrees)`, `worldPosition()`, `forward()` / `right()` / `up()`, `lookAt(point)`. Euler angles are in DEGREES and +Z is forward. On a camera entity, `transform().forward()` points opposite to its view direction (a camera looks along its local -Z).
  - `this.gameObject()` returns the `GameObject`; `this.entityId()` returns the `int` entity id used by the static APIs (`Entity::`, `Physics::`, ...).
  - `this.log(msg)`, `this.logWarn(msg)` and `this.logError(msg)` log with the entity name as prefix; `Log::info(msg)`, `Log::warn(msg)` and `Log::error(msg)` (import lib/engine/Log.mt) log without it.
  - Engine wrappers expose METHODS, never properties: `this.transform().localPosition()`, not `.position`.
- Read and write your own fields as `this.field`, as every engine script does.
- Listener interfaces are implemented with `implements`, e.g. `public class Hazard extends Behaviour implements ICollisionListener` (import lib/engine/ICollisionListener.mt); the engine only dispatches to classes that implement the interface. Implement every method the interface declares, each with `@Override` (game/examples/Projectile.mt implements both `onCollisionEnter(int otherEntityId)` and `onCollisionExit(int otherEntityId)`).
)MTDOC";

        constexpr const char* primerLanguage = R"MTDOC(
## 5. Language rules

- Every declaration is typed: `int`, `float`, `bool`, `string`, class types such as `Vec3f`, arrays (`int[] ids = new int[10];`, length via `ids.length`) and nullable types (`GameObject?`). `void` is a return type. There is NO `var`.
- Write float literals with a decimal point: `5.0`, `0.0`.
- Methods: `public function name(Type param, Type other): ReturnType { ... }`; static methods add `static`. Fields: `private float speed = 5.0;`. Constants: `public static final int MAX_LIVES = 3;`.
- `::` for static members, `.` for instance members:
  - static: `Input::isKeyDown(Key::W)`, `Vec3f::zero()`, `Log::info("hi")`, `Entity::self()`
  - instance: `this.transform().forward()`, `direction.normalize()`, `velocity.multiply(2.0)`
- Always qualify a static field or constant with its class, even inside the class that declares it: `Transform` uses `Transform::RAD_TO_DEG` in its own methods, so write `WasdMover::MAX_SPEED` inside `WasdMover`.
- Math types (`Vec3f`, `Vec2f`, `Vec4f`, `Quaternion`, ...) are value classes; `add`, `subtract`, `multiply(float)` and `normalize()` return a NEW value, so assign the result: `direction = direction.add(step);`. Construct with `new Vec3f(x, y, z)`.
- Number to string: wrap with `parsePrimitive(...)`: `this.log("speed=" + parsePrimitive(this.speed));`
- Nullable values: `GameObject? target = GameObject::find("Player"); if (target != null) { ... }`. A null check narrows the type inside `if (x != null) { ... }`, after `x != null &&`, and after an early-exit guard (`if (x == null) { return; }`, or `throw` / `continue` / `break`).
- Control flow: `if` / `else if` / `else`, `while`, `for (int i = 0; i < n; i = i + 1) { }`, `break`, `continue`, `return`, ternary `cond ? a : b`.
- Casts: `(int)someFloat`.
- Lambdas implement a one-method interface: `x -> { ... }` or `() -> { ... }`, passed directly as an argument, e.g. `int listenerId = ScriptEvent::listen("playerDied", () -> { Log::info("Player died"); });` (lib/engine/ScriptEvent.mt, lib/engine/EventCallback.mt).
- Builtin functions (no import): `parsePrimitive(x)`, `sqrt(x)`, `sin(x)`, `cos(x)`, `tan(x)`, `asin(x)`, `acos(x)`, `atan(x)`, `atan2(y, x)` (angles in radians).
- Reserved words, never usable as names: `function return if else while do for final abstract break continue int float bool string void class new static private public protected constructor null true false import from switch case match interface implements extends super default isClassOf async await try catch throw finally annotation`. `match` is the usual trap: never name a variable or field `match`.

## 6. Workflow

1. Read this page. For any other API read `vf://docs/mtype-api/<module>` (catalogue at the end of this page).
2. `script_write` `{"path": "game/WasdMover.mt", "source": "..."}`.
3. `scripts_build`. Each error is `<file>: <diagnostic>`; fix it with `script_write` and build again until `success` is true. `scripts_build` is refused in Play mode, so build before `play_start`.
4. `script_attach` `{"entity": <id>, "path": "game/WasdMover.mt"}`.
5. `play_start`, then `logs_read` with minLevel `warning` for runtime errors (compile errors come back from `scripts_build` itself), then `play_stop` before editing the scene again.
)MTDOC";

        constexpr const char* primerExample = R"MTDOC(
## 7. Verified example: WASD movement

`scripts/game/examples/PlayerMovement.mt` ships with the engine and compiles. It lives in `game/examples/`, hence `../../lib`; a file directly in `game/` uses `../lib/...`. Give your copy a NEW class name (e.g. `WasdMover`): `PlayerMovement` already exists in projects that carry the examples.

```mtype
// PlayerMovement - Example of the VK-1458 OOP scripting style.
//
// Extends Behaviour: entity access via this.transform()/this.gameObject(),
// lifecycle hooks inherited from the base and overridden selectively.
// WASD moves the entity in its own facing plane (+Z forward convention).

import * from "../../lib/engine/oop/Behaviour.mt";
import * from "../../lib/engine/Input.mt";
import * from "../../lib/engine/Key.mt";
import * from "../../lib/math/Vec3f.mt";

@Script
public class PlayerMovement extends Behaviour {
    private float speed = 5.0;

    public constructor() : super() {
    }

    @Override
    public function onStart(): void {
        this.log("PlayerMovement ready");
    }

    @Override
    public function onUpdate(float deltaTime): void {
        Vec3f direction = Vec3f::zero();

        if (Input::isKeyDown(Key::W)) {
            direction = direction.add(this.transform().forward());
        }
        if (Input::isKeyDown(Key::S)) {
            direction = direction.subtract(this.transform().forward());
        }
        if (Input::isKeyDown(Key::D)) {
            direction = direction.add(this.transform().right());
        }
        if (Input::isKeyDown(Key::A)) {
            direction = direction.subtract(this.transform().right());
        }

        if (direction.lengthSquared() > 0.0) {
            this.transform().translate(direction.normalize().multiply(this.speed * deltaTime));
        }
    }
}
```
)MTDOC";

        constexpr const char* primerAnimation = R"MTDOC(
## 8. Character animation

A skinned mesh entity with an Animator plays its `.vfAnimator` state machine. A script drives it through the animator's PARAMETERS and the transitions do the switching: build them with `animation_set_state_machine`, read them back with `animation_get_state_machine`.

- Static API (lib/engine/Animator.mt), keyed by the `int` entity id: `Animator::setFloat(entityId, name, value)`, `Animator::setInt(entityId, name, value)`, `Animator::setBool(entityId, name, value)`, `Animator::setTrigger(entityId, name)`, `Animator::hasAnimator(entityId)`, `Animator::getCurrentState(entityId)` (the base layer's state name, `""` without an animator), `Animator::isBlending(entityId)`, `Animator::getNormalizedTime(entityId)`, and `Animator::forceTransitionTo(entityId, stateName, blendSeconds)` to jump to a state by name. Full signatures: `vf://docs/mtype-api/engine/Animator`.
- Object style: `this.gameObject().animator()` returns an `AnimatorComponent` (lib/engine/oop/AnimatorComponent.mt) with the same calls without the id: `this.gameObject().animator().setFloat("speed", speed);`, `currentState()`, `isBlending()`, `exists()`.
- A parameter is found by its exact name, and the setter must match its type: `setFloat` for Float, `setInt` for Int, `setBool` for Bool, `setTrigger` for Trigger (`animation_get_state_machine` lists the names and types). Nothing reports a mismatch: a misspelled name stores a value no transition reads, and the wrong setter makes the conditions compare the wrong way or never pass.
- Set the parameters in `onUpdate` every frame, not only when they change: any save of the `.vfAnimator` (`animation_set_state_machine`, the Animator Editor) re-creates its animators with the default parameter values, and every Play session starts from the defaults (Stop restores the scene).
- An imported character is often several skinned mesh entities, each with its own animator: set the parameter on each of them. `Entity::getChildren(entityId)` returns the ids of the DIRECT children (`int[]`) and `Animator::hasAnimator(id)` tells which have an animator; setting a parameter on an entity without an animator does nothing.
- Animation events are NOT delivered to scripts in this build: `Animator::pollEvent` always returns `""` and `IAnimationEventListener` callbacks never run. Time gameplay from the script instead, e.g. with `Animator::getCurrentState` and `Animator::getNormalizedTime`.

Verified example: `scripts/game/examples/AnimatorLocomotion.mt` ships with the engine (quoted verbatim). Give your copy a NEW class name: `AnimatorLocomotion` already exists in projects that carry the examples. It expects a Float parameter `speed` and the states `Idle` (default) and `Run`, with Idle -> Run when `speed > 0.1` and Run -> Idle when `speed <= 0.1`. WASD moves the entity as in section 7, and `speed` is the move speed while a key is held, else `0.0`. The value can come from anything: elapsed time, `this.gameObject().navAgent().velocity().length()`, `this.gameObject().rigidBody().linearVelocity().length()`.

```mtype
// AnimatorLocomotion - Example of driving an Animator parameter from a script.
//
// Expects an animator with a Float parameter "speed" and two states: Idle
// (the default state) and Run, with Idle -> Run when speed > 0.1 and
// Run -> Idle when speed <= 0.1. WASD moves the entity like PlayerMovement;
// "speed" is the move speed while a key is held, else 0.0.
//
// Attach it to the character's root entity. An imported character is often
// several skinned mesh entities, each with its own animator, so the value is
// set on this entity and on every direct child that has an animator. It is
// set every frame: saving the .vfAnimator re-creates its animators with the
// default parameter values.

import * from "../../lib/engine/oop/Behaviour.mt";
import * from "../../lib/engine/Animator.mt";
import * from "../../lib/engine/Entity.mt";
import * from "../../lib/engine/Input.mt";
import * from "../../lib/engine/Key.mt";
import * from "../../lib/math/Vec3f.mt";

@Script
public class AnimatorLocomotion extends Behaviour {
    private float moveSpeed = 3.0;

    public constructor() : super() {
    }

    @Override
    public function onUpdate(float deltaTime): void {
        Vec3f direction = Vec3f::zero();

        if (Input::isKeyDown(Key::W)) {
            direction = direction.add(this.transform().forward());
        }
        if (Input::isKeyDown(Key::S)) {
            direction = direction.subtract(this.transform().forward());
        }
        if (Input::isKeyDown(Key::D)) {
            direction = direction.add(this.transform().right());
        }
        if (Input::isKeyDown(Key::A)) {
            direction = direction.subtract(this.transform().right());
        }

        float speed = 0.0;
        if (direction.lengthSquared() > 0.0) {
            this.transform().translate(direction.normalize().multiply(this.moveSpeed * deltaTime));
            speed = this.moveSpeed;
        }

        int root = this.entityId();
        Animator::setFloat(root, "speed", speed);
        int[] children = Entity::getChildren(root);
        for (int i = 0; i < children.length; i = i + 1) {
            int child = children[i];
            if (Animator::hasAnimator(child)) {
                Animator::setFloat(child, "speed", speed);
            }
        }
    }
}
```
)MTDOC";

        constexpr const char* primerEffects = R"MTDOC(
## 9. Visual effects

An effect is a `.vfVFX` file: one emitter and its node graph. `vfx_list_assets` lists the project's effects; the engine ships ready-made ones in its assets/vfx/library folder, which a project uses as a copy at `assets/vfx/library`. Scripts drive effects through the static class in lib/engine/VFX.mt (full signatures: `vf://docs/mtype-api/engine/VFX`):

- `VFX::spawnAt(path, x, y, z)` plays a one-shot effect at a world position and returns its instance id (`int`); the instance destroys itself once it has finished. `path` is relative to the project folder (absolute paths work too), e.g. `"assets/vfx/library/sparks.vfVFX"`. It returns `0` when the file does not exist or the effect is disabled at the current quality tier, so check for `0`.
- `VFX::spawnAtLooping(path, x, y, z)` plays an effect that keeps running: keep the id and end it with `VFX::destroyInstance(id)`, or with `VFX::stopInstance(id)`, which lets the live particles finish. `VFX::setInstancePosition(id, x, y, z)` moves it and `VFX::attachToSocket(id, entityId, socketName)` makes it follow a socket.
- Everything a script spawns, instances and combos alike, is destroyed when Play stops.
- Change a spawned instance with `VFX::setOverride(id, name, value)` for a number (`"startSize"`, `"spawnRate"`, `"lifetime"`, `"startSpeed"`, `"blendMode"` 0 alpha / 1 additive / 2 premultiplied / 3 multiply, `"sortOrder"` -256 to 256 where higher draws later, ...), `VFX::setOverrideVec3(id, name, x, y, z)` for a direction or `"shapeDimensions"`, and `VFX::setOverrideTint(id, r, g, b, a)` to multiply every authored colour (1, 1, 1, 1 = as authored; above 1 brightens). They return `false` for an unknown name (and for a blendMode outside 0-3 or a negative tint). `VFX::setOverrideColor(id, r, g, b, a)` sets only the START colour, which a colour-over-lifetime gradient (most library effects have one) overwrites every frame: prefer `VFX::setOverrideTint`.
- An entity's VFX component (added with `vfx_add_emitter`) starts by itself when Play starts if its autoPlay is on; `VFX::play(entityId)` starts it and `VFX::stop(entityId)` stops it. Both act on the instance the component gets when Play starts.
- `ICollisionListener` (lib/engine/ICollisionListener.mt) passes only the other entity's id to `onCollisionEnter(int otherEntityId)`, no contact point: spawn the effect at `this.transform().worldPosition()`, as the example below does.
- `IVFXEventListener` (lib/engine/IVFXEventListener.mt) declares `onVFXParticleEvent(int eventType, Vec3f position, Vec3f velocity, int entityId)`, eventType 0 = spawn, 1 = death, 2 = collision, 3 = lifetime threshold. Every script that implements it receives the events of every effect, so filter on `eventType` and on `entityId` (the entity whose VFX component owns the effect, `0` for an instance from `VFX::spawnAt`). It fires only for the event types an effect enables with "notify" or a sub-effect, and the engine's library effects enable none.
- `IVFXComboCueListener` (lib/engine/IVFXComboCueListener.mt) declares `onComboCue(int comboId, string cueName, ComboCuePayload payload)`: it runs when a combo (a `.vfVFXSequence`, spawned with `VFX::spawnCombo(path, x, y, z)`) reaches one of its cues, or when a script calls `VFX::triggerComboCue(comboId, cueName)`. Check the payload's `hasPosition`, `hasColor` and `hasScalar` flags before reading `position()`, `colorR` / `colorG` / `colorB` / `colorA` or `scalar`.

Verified example: `scripts/game/examples/HitEffect.mt` ships with the engine (quoted verbatim). Give your copy a NEW class name: `HitEffect` already exists in projects that carry the examples. Attach it to an entity with a RigidBody and a Collider: when that entity starts touching another solid entity, it spawns sparks at its own position. The effect path assumes the engine's library is copied into the project as `assets/vfx/library`.

```mtype
// HitEffect - Example of spawning a VFX one-shot on collision (VK-1657).
//
// Collision callbacks carry only the other entity's id, not the contact
// point, so the effect plays at this entity's world position. The path is
// relative to the project folder: copy the engine's assets/vfx/library into
// the project as assets/vfx/library (vfx_list_assets lists what is there).

import * from "../../lib/engine/oop/Behaviour.mt";
import * from "../../lib/engine/ICollisionListener.mt";
import * from "../../lib/engine/VFX.mt";
import * from "../../lib/math/Vec3f.mt";

@Script
public class HitEffect extends Behaviour implements ICollisionListener {
    private string effectPath = "assets/vfx/library/sparks.vfVFX";

    public constructor() : super() {
    }

    @Override
    public function onCollisionEnter(int otherEntityId): void {
        Vec3f at = this.transform().worldPosition();
        int instanceId = VFX::spawnAt(this.effectPath, at.x, at.y, at.z);
        if (instanceId == 0) {
            this.logWarn("no effect spawned from " + this.effectPath);
        }
    }

    @Override
    public function onCollisionExit(int otherEntityId): void {
    }
}
```
)MTDOC";

        constexpr const char* primerWater = R"MTDOC(
## 10. Water

A scene has at most one ocean, the entity created as `"Ocean"` (`Entity::findByName("Ocean")`, which returns `-1` when there is no ocean or it was renamed), plus any number of lakes and pools (water bodies). The static class in lib/engine/Ocean.mt reads and drives them (full signatures: `vf://docs/mtype-api/engine/Ocean`):

- Queries that cover ANY water: `Ocean::isInOcean(position)` (a `Vec3f`), `Ocean::getOceanHeightAt(x, z)` (the surface height: a lake or pool's level inside its rectangle, else the ocean's surface with its waves; `0.0` where there is no water, so check `isInOcean` when y = 0 is a real answer), `Ocean::getWaterDepthAt(x, z)` (`0.0` where there is no water, very large over open sea), `Ocean::isCameraUnderwater()` (the primary camera) and `Ocean::isEntityInWater(entityId)` (a dynamic rigid body, updated by the physics step in Play mode).
- A HIDDEN ocean (`ocean_enable {enabled:false}`, or "Ocean surface visible" off in the editor's Ocean window) keeps its settings but has no surface: the queries above answer only for lakes and pools, nothing floats in the sea and the waves stop (lakes with waves go flat too).
- Calls that take the ocean entity id: `Ocean::getBaseHeight(id)` (its level), `Ocean::getDensity(id)`, `Ocean::getDrag(id)`, `Ocean::getBuoyancyStrength(id)`, `Ocean::setPhysicsSettings(id, density, drag, buoyancyStrength)` (drag and buoyancyStrength tune floating in every water, lakes and pools included; density is unused), `Ocean::setWeatherDriven(id, enabled)` and `Ocean::setVisualSettings(id, ...)` (colours shared by every lake and pool). Without an id: `Ocean::setSeaState(beaufort, transitionSeconds)` (0 = calm .. 12 = hurricane), `Ocean::getSeaState()` and `Ocean::addImpulse(x, z, radius, strength)` (a ripple on any water; dropped while the ripple simulation is off, see `Ocean::setRippleSimEnabled(true)`).
- `IWaterListener` (lib/engine/IWaterListener.mt) declares `onWaterEnter(float verticalSpeed)`, `onWaterExit()`, `onEntityWaterEnter(int entityId, float x, float y, float z, float verticalSpeed)` and `onEntityWaterExit(int entityId)`; implement all four. The first two run for the script's own entity, the other two for EVERY entity. Only dynamic rigid bodies are tracked, in Play mode, in water whose physics is on. `verticalSpeed` is the vertical velocity at impact in m/s, NEGATIVE while falling in; x, y, z is the entity's position, not the water surface.
- A project created from an old template may carry an old lib/engine/Ocean.mt and no lib/engine/IWaterListener.mt (the core API below then lacks functions or says it is not found): copy the engine's assets/scripts/lib into the project's scripts/lib.

Example: `scripts/game/examples/WaterSplash.mt` ships with the engine (quoted verbatim). Give your copy a NEW class name: `WaterSplash` already exists in projects that carry the examples. Attach it to an entity with a dynamic RigidBody and a Collider: when the entity drops into water faster than 1 m/s, a splash plays on the surface above it. The effect path assumes the engine's library is copied into the project as `assets/vfx/library`.

```mtype
// WaterSplash - Example of reacting to water through IWaterListener (VK-1658).
//
// Attach it to an entity with a dynamic RigidBody and a Collider. When the
// entity drops into water (a lake, a pool or the visible ocean) faster than
// 1 m/s, a splash plays on the surface above it. verticalSpeed is the vertical
// velocity at impact: negative while falling. The path is relative to the
// project folder: copy the engine's assets/vfx/library into the project as
// assets/vfx/library (vfx_list_assets lists what is there).

import * from "../../lib/engine/oop/Behaviour.mt";
import * from "../../lib/engine/IWaterListener.mt";
import * from "../../lib/engine/Ocean.mt";
import * from "../../lib/engine/VFX.mt";
import * from "../../lib/math/Vec3f.mt";

@Script
public class WaterSplash extends Behaviour implements IWaterListener {
    private string effectPath = "assets/vfx/library/rain_splash.vfVFX";

    public constructor() : super() {
    }

    @Override
    public function onWaterEnter(float verticalSpeed): void {
        if (verticalSpeed < -1.0) {
            Vec3f at = this.transform().worldPosition();
            float surface = Ocean::getOceanHeightAt(at.x, at.z);
            int instanceId = VFX::spawnAt(this.effectPath, at.x, surface, at.z);
            if (instanceId == 0) {
                this.logWarn("no splash spawned from " + this.effectPath);
            }
        }
    }

    @Override
    public function onWaterExit(): void {
    }

    @Override
    public function onEntityWaterEnter(int entityId, float x, float y, float z,
                                       float verticalSpeed): void {
    }

    @Override
    public function onEntityWaterExit(int entityId): void {
    }
}
```
)MTDOC";

        bool isIdentChar(char c)
        {
            return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
        }

        bool isSpace(char c)
        {
            return std::isspace(static_cast<unsigned char>(c)) != 0;
        }

        bool startsWithWord(std::string_view text, std::string_view word)
        {
            return text.size() >= word.size() && text.substr(0, word.size()) == word &&
                (text.size() == word.size() || !isIdentChar(text[word.size()]));
        }

        // Collapses whitespace runs to one space and trims; drops the space after
        // '(' and before ')' or ',' that a multi-line parameter list leaves behind.
        std::string normalise(std::string_view text)
        {
            std::string out;
            bool pendingSpace = false;
            for (char c : text)
            {
                if (isSpace(c))
                {
                    pendingSpace = !out.empty();
                    continue;
                }
                if (pendingSpace && out.back() != '(' && c != ')' && c != ',')
                {
                    out += ' ';
                }
                pendingSpace = false;
                out += c;
            }
            return out;
        }

        // Drops leading annotations ("@Script", "@Override", "@Name(args)").
        std::string_view stripAnnotations(std::string_view text)
        {
            while (!text.empty() && text.front() == '@')
            {
                std::size_t end = 1;
                while (end < text.size() && (isIdentChar(text[end]) || text[end] == '.'))
                {
                    ++end;
                }
                std::size_t next = end;
                while (next < text.size() && text[next] == ' ')
                {
                    ++next;
                }
                if (next < text.size() && text[next] == '(')
                {
                    int depth = 0;
                    for (; next < text.size(); ++next)
                    {
                        if (text[next] == '(')
                        {
                            ++depth;
                        }
                        else if (text[next] == ')' && --depth == 0)
                        {
                            ++next;
                            break;
                        }
                    }
                    end = next;
                }
                text.remove_prefix(std::min(end, text.size()));
                while (!text.empty() && text.front() == ' ')
                {
                    text.remove_prefix(1);
                }
            }
            return text;
        }

        struct TypeDecl
        {
            std::string name;
            bool visible = true;
            bool isInterface = false;
        };

        std::optional<TypeDecl> parseTypeDecl(const std::string& text)
        {
            static const std::regex pattern(
                R"(^(?:(public|private|protected) )?(?:(?:abstract|value|final|static) )*(class|interface) ([A-Za-z_][A-Za-z0-9_]*))");
            std::smatch match;
            if (!std::regex_search(text, match, pattern))
            {
                return std::nullopt;
            }
            TypeDecl decl;
            decl.visible = match[1].str() != "private" && match[1].str() != "protected";
            decl.isInterface = match[2].str() == "interface";
            decl.name = match[3].str();
            return decl;
        }

        // True when a space-separated word before the first '(' is "function" or
        // "constructor" (methods, constructors, interface methods).
        bool isCallable(std::string_view text)
        {
            const std::string_view head = text.substr(0, text.find('('));
            std::size_t start = 0;
            while (start < head.size())
            {
                std::size_t end = head.find(' ', start);
                if (end == std::string_view::npos)
                {
                    end = head.size();
                }
                const std::string_view word = head.substr(start, end - start);
                if (word == "function" || word == "constructor")
                {
                    return true;
                }
                start = end + 1;
            }
            return false;
        }

        // "public constructor(int a) : super(a)" -> "public constructor(int a)".
        std::string cutAfterParameters(const std::string& text)
        {
            const std::size_t open = text.find('(');
            if (open == std::string::npos)
            {
                return text;
            }
            int depth = 0;
            for (std::size_t i = open; i < text.size(); ++i)
            {
                if (text[i] == '(')
                {
                    ++depth;
                }
                else if (text[i] == ')' && --depth == 0)
                {
                    return text.substr(0, i + 1);
                }
            }
            return text;
        }

        bool isVisibleMember(std::string_view text, bool inInterface)
        {
            if (startsWithWord(text, "public"))
            {
                return true;
            }
            return inInterface && !startsWithWord(text, "private") && !startsWithWord(text, "protected");
        }

        // A member statement ended by ';' (field, constant, abstract or interface
        // method) or by '{' (method / constructor body, or a block initializer).
        // Returns the one-line signature, or nothing when it is not part of the API.
        std::optional<std::string> memberSignature(std::string_view text, bool inInterface, bool endsWithBrace)
        {
            if (text.empty() || !isVisibleMember(text, inInterface))
            {
                return std::nullopt;
            }
            std::string member(text);
            if (isCallable(member))
            {
                if (member.find("constructor") != std::string::npos && member.find("function") == std::string::npos)
                {
                    member = cutAfterParameters(member);
                }
                return member + ";";
            }
            if (endsWithBrace)
            {
                // Field with a block initializer (lambda / array): keep the declaration.
                const std::size_t equals = member.find('=');
                if (equals == std::string::npos)
                {
                    return std::nullopt;
                }
                return normalise(std::string_view(member).substr(0, equals)) + ";";
            }
            return member + ";";
        }

        bool readText(const fs::path& file, std::string& out)
        {
            std::error_code ec;
            const std::uintmax_t bytes = fs::file_size(file, ec);
            if (ec || bytes > maxModuleBytes)
            {
                return false;
            }
            std::ifstream stream(file, std::ios::binary);
            if (!stream)
            {
                return false;
            }
            out.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
            return true;
        }

        std::string importLine(const std::string& module)
        {
            return "`import * from \"../lib/" + module + ".mt\";`";
        }

        std::string joinTypeNames(const std::vector<ApiType>& types)
        {
            std::string names;
            for (const ApiType& type : types)
            {
                names += names.empty() ? "" : ", ";
                names += type.name;
            }
            return names.empty() ? "(no public types)" : names;
        }
    }

    std::string stripComments(std::string_view source)
    {
        enum class State
        {
            Code,
            LineComment,
            BlockComment,
            String
        };

        std::string out;
        out.reserve(source.size());
        State state = State::Code;
        for (std::size_t i = 0; i < source.size(); ++i)
        {
            const char c = source[i];
            const char next = i + 1 < source.size() ? source[i + 1] : '\0';
            switch (state)
            {
            case State::Code:
                if (c == '/' && next == '/')
                {
                    state = State::LineComment;
                    out += "  ";
                    ++i;
                }
                else if (c == '/' && next == '*')
                {
                    state = State::BlockComment;
                    out += "  ";
                    ++i;
                }
                else
                {
                    if (c == '"')
                    {
                        state = State::String;
                    }
                    out += c;
                }
                break;
            case State::LineComment:
                if (c == '\n')
                {
                    state = State::Code;
                }
                out += (c == '\n' || c == '\r') ? c : ' ';
                break;
            case State::BlockComment:
                if (c == '*' && next == '/')
                {
                    state = State::Code;
                    out += "  ";
                    ++i;
                }
                else
                {
                    out += (c == '\n' || c == '\r') ? c : ' ';
                }
                break;
            case State::String:
                out += c;
                if (c == '\\' && i + 1 < source.size())
                {
                    out += next;
                    ++i;
                }
                else if (c == '"' || c == '\n')
                {
                    state = State::Code;  // closed (or unterminated: recover at end of line)
                }
                break;
            }
        }
        return out;
    }

    std::vector<ApiType> parseApi(std::string_view source)
    {
        const std::string code = stripComments(source);

        // An open type body: members are the statements at exactly `depth`.
        struct Frame
        {
            std::size_t typeIndex;
            int depth;
            bool visible;
            bool isInterface;
        };

        std::vector<ApiType> types;
        std::vector<Frame> frames;
        int depth = 0;
        std::string statement;
        bool inString = false;

        for (std::size_t i = 0; i < code.size(); ++i)
        {
            const char c = code[i];
            if (inString)
            {
                statement += c;
                if (c == '\\' && i + 1 < code.size())
                {
                    statement += code[++i];
                }
                else if (c == '"' || c == '\n')
                {
                    inString = false;
                }
                continue;
            }
            if (c == '"')
            {
                inString = true;
                statement += c;
                continue;
            }
            if (c != '{' && c != '}' && c != ';')
            {
                statement += c;
                continue;
            }

            const std::string normalised = normalise(statement);
            const std::string text(stripAnnotations(normalised));
            statement.clear();

            const bool inTypeBody = !frames.empty() && frames.back().depth == depth;
            if (c == '{')
            {
                std::optional<TypeDecl> decl = (depth == 0 || inTypeBody) ? parseTypeDecl(text) : std::nullopt;
                if (decl.has_value())
                {
                    const bool visible = decl->visible && (frames.empty() || frames.back().visible);
                    if (visible)
                    {
                        ApiType type;
                        type.name = decl->name;
                        type.header = text;
                        type.isInterface = decl->isInterface;
                        types.push_back(std::move(type));
                    }
                    frames.push_back({types.empty() ? 0 : types.size() - 1, depth + 1, visible, decl->isInterface});
                }
                else if (inTypeBody && frames.back().visible)
                {
                    if (auto member = memberSignature(text, frames.back().isInterface, true))
                    {
                        types[frames.back().typeIndex].members.push_back(std::move(*member));
                    }
                }
                ++depth;
            }
            else if (c == ';')
            {
                if (inTypeBody && frames.back().visible)
                {
                    if (auto member = memberSignature(text, frames.back().isInterface, false))
                    {
                        types[frames.back().typeIndex].members.push_back(std::move(*member));
                    }
                }
            }
            else
            {
                if (depth > 0)
                {
                    --depth;
                }
                if (!frames.empty() && frames.back().depth > depth)
                {
                    frames.pop_back();
                }
            }
        }
        return types;
    }

    std::string extractApi(std::string_view source)
    {
        std::string out;
        for (const ApiType& type : parseApi(source))
        {
            out += "```mtype\n" + type.header + " {\n";
            for (const std::string& member : type.members)
            {
                out += "    " + member + "\n";
            }
            out += "}\n```\n\n";
        }
        if (out.empty())
        {
            out = "_No public declarations._\n\n";
        }
        return out;
    }

    const std::string& primer()
    {
        static const std::string text = std::string(primerOverview) + primerBehaviour + primerLanguage + primerExample +
                                        primerAnimation + primerEffects + primerWater;
        return text;
    }

    const std::vector<std::string>& coreModules()
    {
        static const std::vector<std::string> modules{
            "engine/oop/Behaviour",
            "engine/oop/Transform",
            "engine/oop/GameObject",
            "engine/Input",
            "engine/Key",
            "engine/Time",
            "engine/Log",
            "engine/Animator",
            "engine/VFX",
            "engine/Ocean",
            "engine/IWaterListener",
            "math/Vec3f"
        };
        return modules;
    }

    bool isValidModuleId(std::string_view module)
    {
        if (module.empty() || module.size() > 256 || module.front() == '/' || module.back() == '/' ||
            module.find("//") != std::string_view::npos)
        {
            return false;
        }
        return std::all_of(module.begin(), module.end(), [](char c) { return isIdentChar(c) || c == '/'; });
    }

    std::string buildMTypeApiDoc(const fs::path& scriptsRoot)
    {
        std::string doc = primer();

        const fs::path lib = scriptsRoot / "lib";
        std::error_code ec;
        if (!fs::is_directory(lib, ec))
        {
            doc += "\n# API signatures\n\nsignatures unavailable: project has no scripts/lib\n";
            return doc;
        }

        doc += "\n# Core API (signatures parsed from this project's scripts/lib)\n\n"
               "Import lines are for a file directly in scripts/game/; add one `../` per sub-folder.\n\n";
        for (const std::string& module : coreModules())
        {
            doc += "## " + module + "\n\n" + importLine(module) + "\n\n";
            std::string source;
            if (readText(lib / pathFromUtf8(module + ".mt"), source))
            {
                doc += extractApi(source);
            }
            else
            {
                doc += "_Not found in this project's scripts/lib._\n\n";
            }
        }

        std::vector<std::pair<std::string, fs::path>> modules;
        fs::recursive_directory_iterator it(lib, fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            std::error_code entryEc;
            if (!it->is_regular_file(entryEc) || !detail::componentEquals(it->path().extension(), ".mt"))
            {
                continue;
            }
            fs::path relative = it->path().lexically_relative(lib);
            relative.replace_extension();
            std::string id = genericPathToUtf8(relative);
            if (!isValidModuleId(id))
            {
                continue;  // unreachable through vf://docs/mtype-api/{module}
            }
            if (std::find(coreModules().begin(), coreModules().end(), id) == coreModules().end())
            {
                modules.emplace_back(std::move(id), it->path());
            }
        }
        std::sort(modules.begin(), modules.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        doc += "# Other modules\n\n"
               "Read one with `vf://docs/mtype-api/<module>` (e.g. `vf://docs/mtype-api/engine/Physics`). "
               "Each line: module, its types, the import line from a file directly in scripts/game/.\n\n";
        for (const auto& [id, file] : modules)
        {
            std::string source;
            const std::string types = readText(file, source) ? joinTypeNames(parseApi(source)) : "(unreadable)";
            doc += "- `" + id + "`: " + types + ": " + importLine(id) + "\n";
        }
        return doc;
    }

    std::string buildModuleDoc(const fs::path& scriptsRoot, const std::string& module)
    {
        std::string id = module;
        if (id.size() > 3 && id.ends_with(".mt"))
        {
            id.resize(id.size() - 3);
        }
        if (!isValidModuleId(id))
        {
            throw ArgError("invalid mType module '" + module + "': use a scripts/lib-relative id such as "
                           "'engine/Physics' (letters, digits, '_' and '/')");
        }

        const fs::path lib = scriptsRoot / "lib";
        std::error_code ec;
        if (!fs::is_directory(lib, ec))
        {
            throw ResourceNotFound("signatures unavailable: project has no scripts/lib");
        }

        std::string error;
        std::optional<fs::path> file = resolveInside(lib, id + ".mt", error, ".mt");
        if (!file.has_value())
        {
            throw ArgError(error);
        }
        std::string source;
        if (!fs::is_regular_file(*file, ec) || !readText(*file, source))
        {
            throw ResourceNotFound("unknown mType module '" + id +
                                   "'; the catalogue at the end of vf://docs/mtype-api lists them");
        }

        // Report the on-disk spelling (NTFS matched case-insensitively).
        const fs::path canonicalLib = fs::weakly_canonical(lib, ec);
        if (!ec)
        {
            fs::path relative = file->lexically_relative(canonicalLib);
            relative.replace_extension();
            const std::string onDisk = genericPathToUtf8(relative);
            if (isValidModuleId(onDisk))
            {
                id = onDisk;
            }
        }

        return "# mType module `" + id + "`\n\n"
               "Import from a file directly in scripts/game/: " + importLine(id) +
               " (one more `../` per sub-folder). Commented source: script_read `lib/" + id + ".mt`.\n\n" +
               extractApi(source);
    }
}
