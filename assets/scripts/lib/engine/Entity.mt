// Entity - Static utility class for entity/component operations
// Works with entity IDs (int) similar to Log and Time classes
//
// Usage examples:
//   int self = Entity::self();
//   int player = Entity::findByName("Player");
//   int[] enemies = Entity::findAll("Enemy");
//   float[] pos = Entity::getPosition(self);
//   Entity::setPosition(self, 1.0, 2.0, 3.0);

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
    //              "AudioSource2D", "AudioSource3D", "IBL", "Billboard"
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

    // Get local position as float[3] (x, y, z)
    public static function getPosition(int entityId): float[] {
        return _native_entity_getPosition(entityId);
    }

    // Set local position
    public static function setPosition(int entityId, float x, float y, float z): void {
        _native_entity_setPosition(entityId, x, y, z);
    }

    // Get local rotation (Euler angles in degrees) as float[3]
    public static function getRotation(int entityId): float[] {
        return _native_entity_getRotation(entityId);
    }

    // Set local rotation (Euler angles in degrees)
    public static function setRotation(int entityId, float x, float y, float z): void {
        _native_entity_setRotation(entityId, x, y, z);
    }

    // Get local scale as float[3]
    public static function getScale(int entityId): float[] {
        return _native_entity_getScale(entityId);
    }

    // Set local scale
    public static function setScale(int entityId, float x, float y, float z): void {
        _native_entity_setScale(entityId, x, y, z);
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
    // Hierarchy Operations
    // ============================================

    // Get parent entity ID, returns -1 if no parent
    public static function getParent(int entityId): int {
        return _native_entity_getParent(entityId);
    }

    // Get child entity IDs
    public static function getChildren(int entityId): int[] {
        return _native_entity_getChildren(entityId);
    }

    // ============================================
    // Lifecycle
    // ============================================

    // Create a new entity with the given name
    public static function create(string name): int {
        return _native_entity_create(name);
    }

    // Destroy an entity
    public static function destroy(int entityId): void {
        _native_entity_destroy(entityId);
    }
}
