// GameObject - Object wrapper over an entity id (VK-1458 OOP layer).
//
// A stateless view: wraps the immutable id and forwards every query to the
// Entity static facade, so it never goes stale when other systems mutate the
// entity. The public `id` is the deliberate escape hatch to the static API
// (blackboards, statics, BT tasks all speak int ids).
//
// A destroyed entity leaves the wrapper valid-but-dead: exists() turns false
// and forwarded calls degrade exactly like the static API always has.
//
// Usage:
//   GameObject player = GameObject::find("Player");
//   if (player != null && player.exists()) {
//       player.navAgent().setDestination(this.transform().worldPosition());
//   }

import * from "../Entity.mt";
import * from "../ScriptCallback.mt";
import * from "Transform.mt";
import * from "RigidBody.mt";
import * from "Collider.mt";
import * from "CameraComponent.mt";
import * from "AnimatorComponent.mt";
import * from "AudioSource.mt";
import * from "NavAgent.mt";
import * from "MeshRenderer.mt";
import * from "UIElement.mt";

public class GameObject {
    public final int id;

    public constructor(int id) {
        this.id = id;
    }

    // ============================================
    // Static factories
    // ============================================

    // GameObject for the entity this script is attached to. Works in any
    // engine-dispatched context (lifecycle hooks, listener callbacks, BT
    // script tasks) — not only Behaviour subclasses.
    public static function self(): GameObject {
        return new GameObject(Entity::self());
    }

    public static function fromId(int entityId): GameObject {
        return new GameObject(entityId);
    }

    // First entity with the given name, or null
    public static function find(string name): GameObject? {
        int found = Entity::findByName(name);
        if (found < 0) {
            return null;
        }
        return new GameObject(found);
    }

    public static function findAll(string name): GameObject[] {
        int[] ids = Entity::findAll(name);
        GameObject[] objects = new GameObject[ids.length];
        for (int i = 0; i < ids.length; i = i + 1) {
            objects[i] = new GameObject(ids[i]);
        }
        return objects;
    }

    // Create a new empty entity at the scene root
    public static function create(string name): GameObject {
        return new GameObject(Entity::create(name));
    }

    // ============================================
    // Identity / liveness
    // ============================================

    public function exists(): bool {
        return Entity::isValid(this.id);
    }

    public function equals(GameObject other): bool {
        return this.id == other.id;
    }

    public function hashCode(): int {
        return this.id;
    }

    // ============================================
    // Name / active state
    // ============================================

    public function name(): string {
        return Entity::getName(this.id);
    }

    public function setName(string name): void {
        Entity::setName(this.id, name);
    }

    public function isActive(): bool {
        return Entity::isActive(this.id);
    }

    public function setActive(bool active): void {
        Entity::setActive(this.id, active);
    }

    // ============================================
    // Hierarchy
    // ============================================

    public function parent(): GameObject? {
        int parentId = Entity::getParent(this.id);
        if (parentId < 0) {
            return null;
        }
        return new GameObject(parentId);
    }

    // Pass null to move the entity to the scene root
    public function setParent(GameObject? parent): bool {
        if (parent == null) {
            return Entity::moveToRoot(this.id);
        }
        return Entity::setParent(this.id, parent.id);
    }

    public function children(): GameObject[] {
        int[] ids = Entity::getChildren(this.id);
        GameObject[] objects = new GameObject[ids.length];
        for (int i = 0; i < ids.length; i = i + 1) {
            objects[i] = new GameObject(ids[i]);
        }
        return objects;
    }

    // ============================================
    // Lifecycle
    // ============================================

    // Destroy this entity and all its children
    public function destroy(): void {
        Entity::destroy(this.id);
    }

    // ============================================
    // Components — stringly-typed (ComponentType constants)
    // ============================================

    public function hasComponent(string componentType): bool {
        return Entity::hasComponent(this.id, componentType);
    }

    public function addComponent(string componentType): bool {
        return Entity::addComponent(this.id, componentType);
    }

    public function removeComponent(string componentType): bool {
        return Entity::removeComponent(this.id, componentType);
    }

    public function componentTypes(): string[] {
        return Entity::getComponents(this.id);
    }

    // ============================================
    // Components — typed accessors
    // ============================================
    // Always return a (non-null) wrapper view; call exists() on the wrapper
    // when the component may be absent. mType generics are not reified, so
    // per-type accessors replace Unity's getComponent<T>() — same type
    // safety, full autocomplete discovery.

    public function transform(): Transform {
        return new Transform(this.id);
    }

    public function rigidBody(): RigidBody {
        return new RigidBody(this.id);
    }

    public function collider(): Collider {
        return new Collider(this.id);
    }

    public function camera(): CameraComponent {
        return new CameraComponent(this.id);
    }

    public function animator(): AnimatorComponent {
        return new AnimatorComponent(this.id);
    }

    public function audioSource(): AudioSource {
        return new AudioSource(this.id);
    }

    public function navAgent(): NavAgent {
        return new NavAgent(this.id);
    }

    public function meshRenderer(): MeshRenderer {
        return new MeshRenderer(this.id);
    }

    public function uiElement(): UIElement {
        return new UIElement(this.id);
    }

    // ============================================
    // Scripts / messaging
    // ============================================

    // Get another script instance on this entity, cast to T
    public function <T> getScript(string className): T {
        return Entity::getScript<T>(this.id, className);
    }

    public function hasScriptOfType(string className): bool {
        return Entity::hasScriptOfType(this.id, className);
    }

    public function sendMessage(ScriptCallback callback): void {
        Entity::sendMessage(this.id, callback);
    }

    public function broadcastMessage(ScriptCallback callback): void {
        Entity::broadcastMessage(this.id, callback);
    }
}
