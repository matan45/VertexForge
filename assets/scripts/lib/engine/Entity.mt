// Entity - Static utility class for entity/component operations
// Works with entity IDs (int) similar to Log and Time classes
//
// Usage examples:
//   int self = Entity::self();
//   int player = Entity::findByName("Player");
//   int[] enemies = Entity::findAll("Enemy");
//   Vec3f pos = Entity::getPosition(self);
//   Entity::setPosition(self, new Vec3f(1.0, 2.0, 3.0));

import * from "../math/Vec3f.mt";
import * from "ScriptCallback.mt";

public class Entity {
    public constructor() {
    }

    // ============================================
    // Static Query Methods
    // ============================================

    // Get the entity ID this script is attached to
    public static function self(): int {
        return _native_entity_getSelf();
    }

    // Find first entity by name, returns -1 if not found
    public static function findByName(string name): int {
        return _native_entity_findByName(name);
    }

    // Find all entities with the given name
    public static function findAll(string name): int[] {
        return _native_entity_findAll(name);
    }

    // Find all entities with a specific component type
    // Valid types: "Transform", "Camera", "Mesh", "Material", "Script",
    //              "AudioSource2D", "AudioSource3D", "IBL", "Billboard", "Text",
    //              "UICanvas", "UIRect", "UIImage", "UIScroll", "UILayoutGroup",
    //              "UILabel", "UIButton", "UITextInput", "UICheckbox", "UIDropdown",
    //              "UITabs", "UISlider", "UIProgressBar"
    // Use ComponentType constants for type safety (e.g., ComponentType::UI_BUTTON)
    public static function findWithComponent(string componentType): int[] {
        return _native_entity_findWithComponent(componentType);
    }

    // ============================================
    // Entity Properties
    // ============================================

    // Check if an entity ID is valid
    public static function isValid(int entityId): bool {
        if (entityId < 0) {
            return false;
        }
        return _native_entity_isValid(entityId);
    }

    // Check if an entity is active
    // When inactive, the entity and all its components are disabled
    public static function isActive(int entityId): bool {
        return _native_entity_isActive(entityId);
    }

    // Set entity active state
    // When set to false, the entity and all its components become inactive
    public static function setActive(int entityId, bool active): void {
        _native_entity_setActive(entityId, active);
    }

    // Get entity name
    public static function getName(int entityId): string {
        return _native_entity_getName(entityId);
    }

    // Set entity name
    public static function setName(int entityId, string name): void {
        _native_entity_setName(entityId, name);
    }

    // ============================================
    // Transform Operations
    // ============================================

    // Get local position
    public static function getPosition(int entityId): Vec3f {
        float[] v = _native_entity_getPosition(entityId);
        return new Vec3f(v[0], v[1], v[2]);
    }

    // Set local position
    public static function setPosition(int entityId, Vec3f position): void {
        _native_entity_setPosition(entityId, position.x, position.y, position.z);
    }

    // Get local rotation (Euler angles in degrees)
    public static function getRotation(int entityId): Vec3f {
        float[] v = _native_entity_getRotation(entityId);
        return new Vec3f(v[0], v[1], v[2]);
    }

    // Set local rotation (Euler angles in degrees)
    public static function setRotation(int entityId, Vec3f rotation): void {
        _native_entity_setRotation(entityId, rotation.x, rotation.y, rotation.z);
    }

    // Get local scale
    public static function getScale(int entityId): Vec3f {
        float[] v = _native_entity_getScale(entityId);
        return new Vec3f(v[0], v[1], v[2]);
    }

    // Set local scale
    public static function setScale(int entityId, Vec3f scale): void {
        _native_entity_setScale(entityId, scale.x, scale.y, scale.z);
    }

    // Set uniform scale
    public static function setUniformScale(int entityId, float scale): void {
        _native_entity_setScale(entityId, scale, scale, scale);
    }

    // ============================================
    // Component Queries
    // ============================================

    // Check if entity has a component of the given type
    public static function hasComponent(int entityId, string componentType): bool {
        return _native_entity_hasComponent(entityId, componentType);
    }

    // Get list of all component types on this entity
    public static function getComponents(int entityId): string[] {
        return _native_entity_getComponents(entityId);
    }

    // ============================================
    // Component Operations
    // ============================================

    // Add a component to an entity
    // Valid types: see ComponentType constants for full list
    // Returns true if component was added successfully
    public static function addComponent(int entityId, string componentType): bool {
        return _native_entity_addComponent(entityId, componentType);
    }

    // Remove a component from an entity
    // Valid types: see ComponentType constants for full list
    // Returns true if component was removed successfully
    public static function removeComponent(int entityId, string componentType): bool {
        return _native_entity_removeComponent(entityId, componentType);
    }

    // ============================================
    // Hierarchy Operations
    // ============================================

    // Get parent entity ID, returns -1 if no parent (at root)
    public static function getParent(int entityId): int {
        return _native_entity_getParent(entityId);
    }

    // Set the parent of an entity
    // Use parentId = -1 to move entity to scene root
    public static function setParent(int entityId, int parentId): bool {
        return _native_entity_setParent(entityId, parentId);
    }

    // Move entity to scene root (remove from parent)
    public static function moveToRoot(int entityId): bool {
        return _native_entity_setParent(entityId, -1);
    }

    // Get child entity IDs
    public static function getChildren(int entityId): int[] {
        return _native_entity_getChildren(entityId);
    }

    // ============================================
    // Lifecycle
    // ============================================

    // Create a new entity at the scene root
    public static function create(string name): int {
        return _native_entity_create(name);
    }

    // Create a new entity as a child of the specified parent
    public static function createChild(string name, int parentId): int {
        return _native_entity_create(name, parentId);
    }

    // Destroy an entity and all its children
    public static function destroy(int entityId): void {
        _native_entity_destroy(entityId);
    }

    // ============================================
    // Cross-Script Communication
    // ============================================

    // Get a script instance by class name from an entity
    // Returns the script cast to T, or null if not found
    public static function <T> getScript(int entityId, string className): T {
        return (T)_native_entity_getScript(entityId, className);
    }

    // Check if an entity has a loaded script of the given class name
    public static function hasScriptOfType(int entityId, string className): bool {
        return _native_entity_hasScript(entityId, className);
    }

    // Call a callback for each script attached to an entity
    // The callback receives the script object as its argument
    public static function sendMessage(int entityId, ScriptCallback callback): void {
        _native_entity_sendMessage(entityId, callback);
    }

    // Call a callback for each script on the entity and all descendants recursively
    // The callback receives each script object as its argument
    public static function broadcastMessage(int entityId, ScriptCallback callback): void {
        _native_entity_broadcastMessage(entityId, callback);
    }
}
