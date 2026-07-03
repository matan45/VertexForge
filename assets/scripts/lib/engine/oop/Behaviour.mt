// Behaviour - Base class for OOP-style game scripts (VK-1458).
//
// Extend it from an @Script class to get object-style access to your own
// entity: this.gameObject(), this.transform(), name-prefixed logging, and
// default lifecycle hooks you override selectively with @Override.
//
//   @Script
//   public class PlayerMovement extends Behaviour {
//       public constructor() : super() { }
//
//       @Override
//       public function onUpdate(float deltaTime): void {
//           this.transform().translate(this.transform().forward().multiply(deltaTime));
//       }
//   }
//
// Listener interfaces stay interfaces (implements ICollisionListener, ...):
// the engine's per-interface dispatch gate depends on the implements clause.
//
// ENTITY BINDING - the engine injects this instance's entity id into
// vfEntityId right after construction (and re-injects after a @Saveable
// state restore, so a stale saved id can never stick). If the field was not
// injected (older engine, plain `new` in a test), entityId() falls back to
// capturing Entity::self() on first use — in an engine-dispatched call that
// is this script's own entity.
//
// gameObject()/transform() are METHODS, not fields: mType has no property
// getters, and the wrappers are safe to cache only because they hold nothing
// but the id.

import * from "../Entity.mt";
import * from "../Log.mt";
import * from "GameObject.mt";
import * from "Transform.mt";

public class Behaviour {
    // Injected by ScriptingAdapter::loadScript after createObject. -1 = not
    // yet bound. Deliberately NOT "__"-prefixed (the @Saveable JSON
    // serializer rejects double-underscore field names).
    private int vfEntityId = -1;

    private GameObject? vfGameObject = null;
    private Transform? vfTransform = null;

    public constructor() {
    }

    // ============================================
    // Lifecycle hooks — override what you need
    // ============================================
    // The @Script validator accepts these inherited defaults; onLateUpdate/
    // onFixedUpdate/onEnable/onDisable stay optional and host-probed, so they
    // are intentionally NOT declared here (declaring them would subscribe
    // every Behaviour to those dispatches).

    public function onStart(): void {
    }

    public function onUpdate(float deltaTime): void {
    }

    public function onDestroy(): void {
    }

    // ============================================
    // Entity access
    // ============================================

    // The entity this script instance is attached to
    public function entityId(): int {
        if (this.vfEntityId < 0) {
            this.vfEntityId = Entity::self();
        }
        return this.vfEntityId;
    }

    public function gameObject(): GameObject {
        GameObject? cached = this.vfGameObject;
        if (cached != null) {
            return cached;
        }
        GameObject created = new GameObject(this.entityId());
        this.vfGameObject = created;
        return created;
    }

    public function transform(): Transform {
        Transform? cached = this.vfTransform;
        if (cached != null) {
            return cached;
        }
        Transform created = new Transform(this.entityId());
        this.vfTransform = created;
        return created;
    }

    // ============================================
    // Convenience
    // ============================================

    public function setActive(bool active): void {
        this.gameObject().setActive(active);
    }

    // Destroy this script's entity (and children). The engine tears the
    // script down with it.
    public function destroySelf(): void {
        this.gameObject().destroy();
    }

    // Entity-name-prefixed logging
    public function log(string message): void {
        Log::info("[" + this.gameObject().name() + "] " + message);
    }

    public function logWarn(string message): void {
        Log::warn("[" + this.gameObject().name() + "] " + message);
    }

    public function logError(string message): void {
        Log::error("[" + this.gameObject().name() + "] " + message);
    }
}
