# OOP Scripting API (VK-1458)

Unity-style object layer over the mType engine API. It is a **facade on top
of the static API** — `Entity::`, `Physics::`, `UI::` etc. stay exactly as
they are (compat + low-level backend); every wrapper forwards to them and
never calls `_native_*` directly (enforced by `test_script_native_parity`).

Location: `assets/scripts/lib/engine/oop/`.

## Quick start

```mtype
import * from "../../lib/engine/oop/Behaviour.mt";
import * from "../../lib/engine/oop/PrefabRef.mt";
import * from "../../lib/math/Vec3f.mt";

@Script
public class Turret extends Behaviour {
    private PrefabRef shellPrefab = new PrefabRef("assets/prefabs/shell.vfPrefab");

    public constructor() : super() { }

    // onStart/onUpdate/onDestroy have empty defaults in Behaviour — override
    // only what you need (@Script accepts inherited hooks since mType VK-1458).
    @Override
    public function onUpdate(float deltaTime): void {
        GameObject? target = GameObject::find("Player");
        if (target == null) { return; }

        this.transform().lookAt(target.transform().worldPosition());

        GameObject? shell = this.shellPrefab.instantiateAt(
            this.transform().worldPosition().add(this.transform().forward()));
        if (shell != null) {
            shell.rigidBody().applyImpulse(this.transform().forward().multiply(40.0));
        }
    }
}
```

Examples: `assets/scripts/game/examples/{PlayerMovement,BarracksSpawner,Projectile}.mt`.

## The pieces

| Type | Role |
|---|---|
| `Behaviour` | Script base class: `entityId()`, `gameObject()`, `transform()`, `log/logWarn/logError` (name-prefixed), `setActive`, `destroySelf`, default lifecycle hooks |
| `GameObject` | Entity wrapper: `self()/fromId()/find()/findAll()/create()`, name/active, `parent()/setParent()/children()`, `destroy()`, component ops, typed accessors, `getScript<T>` / messaging. Public `id` = escape hatch to the static API |
| `Transform` | Explicit local/world methods: `localPosition()/setLocalPosition`, `localEulerAngles`, `localRotation()` (Quaternion), `localScale`, `translate/rotate`, `worldPosition()/worldEulerAngles()/worldRotation()`, `forward()/right()/up()`, `lookAt()`, `parent()` |
| `Component` + wrappers | `RigidBody`, `Collider`, `CameraComponent`, `AnimatorComponent`, `AudioSource`, `NavAgent`, `MeshRenderer`, `UIElement` — stateless views over the entity id, 1:1 forwards to the static facades |
| `AssetRef<T>` | Typed asset path (`MeshAsset`/`MaterialAsset`/`TextureAsset`/`AudioAsset` phantom tags) |
| `PrefabRef` | Typed prefab path: `instantiate()/instantiateAt()/instantiateUnder()` returning `GameObject?` |
| `@SerializeField` | Forward-looking marker for inspector-exposed fields (annotation only today) |

## Design rules (why it looks like this)

- **Methods, not properties.** mType has no property getters; `transform.position`
  would be a stale snapshot. So: `transform().localPosition()`.
- **Explicit `local*`/`world*` names.** The cheap engine natives are
  local-space; for parented entities local and world differ. There is
  deliberately no bare `position()`.
- **Per-type accessors instead of `getComponent<T>()`.** mType generics are
  not reified — `T` can only cast, never select. `go.rigidBody()`,
  `go.navAgent()`, … give the same type safety with full autocomplete.
- **Accessors never return null.** Wrappers are views; call `exists()` once
  when the component may be absent (`if (rb.exists()) ...`). The natives
  tolerate missing components exactly like the static API always has.
- **Listener callbacks stay interfaces** (`implements ICollisionListener`):
  the engine's dispatch gate keys on the implements clause. Inside a callback
  wrap the other entity with `GameObject::fromId(otherId)`.
- **Stateless views.** Nothing caches names/positions/state — a wrapper holds
  only the `int` id, so it can never go stale.
- **Conventions**: Euler angles in degrees, +Z forward, yaw = rotation.y.

## Entity binding

The engine injects the entity id into `Behaviour.vfEntityId` right after the
script object is created (`ScriptingAdapter::loadScript`), and re-injects it
after a `@Saveable` state restore. If unbound (e.g. constructed manually in a
test), `entityId()` lazily captures `Entity::self()` on first use.

BT ScriptTasks and plain scripts that don't extend Behaviour can still use
the object layer via `GameObject::self()`.

## Mixing with the static API

Both layers are the same engine — mix freely. `gameObject().id` and
`component.entityId` drop you back to ints for blackboards, statics, and
existing code. Nothing about the static API changed.
