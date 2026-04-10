// PluginComponent - Static utility class for accessing plugin-defined components
// Allows mType scripts to read/write plugin component data by component name + field name.
//
// Usage examples:
//   int self = Entity::self();
//
//   // Check if entity has a plugin component
//   if (PluginComponent::has(self, "Health")) {
//       int hp = PluginComponent::getInt(self, "Health", "currentHP");
//       float regen = PluginComponent::getFloat(self, "Health", "regenRate");
//       PluginComponent::setInt(self, "Health", "currentHP", hp - 10);
//   }
//
//   // Vec3 fields return/accept float[3]
//   Vec3f offset = PluginComponent::getVec3(self, "Movement", "offset");
//   PluginComponent::setVec3(self, "Movement", "offset", new Vec3f(1.0, 0.0, 0.0));
//
//   // Array fields (std::vector<T> in C++)
//   int count = PluginComponent::getArraySize(self, "Inventory", "items");
//   int score = PluginComponent::getArrayInt(self, "Stats", "scores", 0);
//   PluginComponent::addArrayInt(self, "Stats", "scores", 42);
//
//   // Map fields (std::map<string, T> in C++)
//   string[] keys = PluginComponent::getMapKeys(self, "Stats", "modifiers");
//   float val = PluginComponent::getMapFloat(self, "Stats", "modifiers", "strength");
//   PluginComponent::setMapFloat(self, "Stats", "modifiers", "strength", 10.0);

import * from "../math/Vec3f.mt";

public class PluginComponent {
    public constructor() {
    }

    // ============================================
    // Component Presence
    // ============================================

    // Check if an entity has a plugin component by name
    public static function has(int entityId, string componentName): bool {
        return _plugin_has(entityId, componentName);
    }

    // ============================================
    // Unified Get/Set (returns Object — cast to target type)
    // Usage: int hp = (int)PluginComponent.get(self, "Stats", "health");
    //        BuffEntry b = (BuffEntry)PluginComponent.get(self, "Comp", "activeBuff");
    // Supports dot-paths: (int)PluginComponent.get(self, "Comp", "weapon.damage")
    // ============================================

    public static function get(int entityId, string componentName, string fieldName): Object {
        return _plugin_get(entityId, componentName, fieldName);
    }

    public static function set(int entityId, string componentName, string fieldName, Object value): bool {
        return _plugin_set(entityId, componentName, fieldName, value);
    }

    // ============================================
    // Object-Returning Container Access (for struct elements)
    // Usage: BuffEntry b = (BuffEntry)PluginComponent.getArrayElement(self, "Comp", "buffs", 0);
    // ============================================

    public static function getArrayElement(int entityId, string componentName, string fieldName, int index): Object {
        return _plugin_getArrayElement(entityId, componentName, fieldName, index);
    }

    public static function setArrayElement(int entityId, string componentName, string fieldName, int index, Object value): bool {
        return _plugin_setArrayElement(entityId, componentName, fieldName, index, value);
    }

    public static function getMapValue(int entityId, string componentName, string fieldName, string key): Object {
        return _plugin_getMapValue(entityId, componentName, fieldName, key);
    }

    public static function setMapValue(int entityId, string componentName, string fieldName, string key, Object value): bool {
        return _plugin_setMapValue(entityId, componentName, fieldName, key, value);
    }

    // ============================================
    // Scalar Getters
    // ============================================

    // Get an integer field value (returns 0 if not found)
    public static function getInt(int entityId, string componentName, string fieldName): int {
        return _plugin_getInt(entityId, componentName, fieldName);
    }

    // Get a float field value (returns 0.0 if not found)
    public static function getFloat(int entityId, string componentName, string fieldName): float {
        return _plugin_getFloat(entityId, componentName, fieldName);
    }

    // Get a boolean field value (returns false if not found)
    public static function getBool(int entityId, string componentName, string fieldName): bool {
        return _plugin_getBool(entityId, componentName, fieldName);
    }

    // Get a string field value (returns "" if not found)
    public static function getString(int entityId, string componentName, string fieldName): string {
        return _plugin_getString(entityId, componentName, fieldName);
    }

    // Get a Vec3f field value
    public static function getVec3(int entityId, string componentName, string fieldName): Vec3f {
        float[] v = _plugin_getVec3(entityId, componentName, fieldName);
        if (v == null) {
            return new Vec3f(0.0, 0.0, 0.0);
        }
        return new Vec3f(v[0], v[1], v[2]);
    }

    // ============================================
    // Scalar Setters
    // ============================================

    // Set an integer field value
    public static function setInt(int entityId, string componentName, string fieldName, int value): bool {
        return _plugin_setInt(entityId, componentName, fieldName, value);
    }

    // Set a float field value
    public static function setFloat(int entityId, string componentName, string fieldName, float value): bool {
        return _plugin_setFloat(entityId, componentName, fieldName, value);
    }

    // Set a boolean field value
    public static function setBool(int entityId, string componentName, string fieldName, bool value): bool {
        return _plugin_setBool(entityId, componentName, fieldName, value);
    }

    // Set a string field value
    public static function setString(int entityId, string componentName, string fieldName, string value): bool {
        return _plugin_setString(entityId, componentName, fieldName, value);
    }

    // Set a Vec3f field value
    public static function setVec3(int entityId, string componentName, string fieldName, Vec3f value): bool {
        float[] v = new float[3];
        v[0] = value.x;
        v[1] = value.y;
        v[2] = value.z;
        return _plugin_setVec3(entityId, componentName, fieldName, v);
    }

    // ============================================
    // Array Operations (std::vector<T> fields)
    // ============================================

    // Get the number of elements in an array field
    public static function getArraySize(int entityId, string componentName, string fieldName): int {
        return _plugin_getArraySize(entityId, componentName, fieldName);
    }

    // Get an int element from an array field
    public static function getArrayInt(int entityId, string componentName, string fieldName, int index): int {
        return (int)_plugin_getArrayElement(entityId, componentName, fieldName, index);
    }

    // Get a float element from an array field
    public static function getArrayFloat(int entityId, string componentName, string fieldName, int index): float {
        return (float)_plugin_getArrayElement(entityId, componentName, fieldName, index);
    }

    // Get a string element from an array field
    public static function getArrayString(int entityId, string componentName, string fieldName, int index): string {
        return (string)_plugin_getArrayElement(entityId, componentName, fieldName, index);
    }

    // Get a bool element from an array field
    public static function getArrayBool(int entityId, string componentName, string fieldName, int index): bool {
        return (bool)_plugin_getArrayElement(entityId, componentName, fieldName, index);
    }

    // Set an element in an array field at the given index
    public static function setArrayInt(int entityId, string componentName, string fieldName, int index, int value): bool {
        return _plugin_setArrayElement(entityId, componentName, fieldName, index, value);
    }

    public static function setArrayFloat(int entityId, string componentName, string fieldName, int index, float value): bool {
        return _plugin_setArrayElement(entityId, componentName, fieldName, index, value);
    }

    public static function setArrayString(int entityId, string componentName, string fieldName, int index, string value): bool {
        return _plugin_setArrayElement(entityId, componentName, fieldName, index, value);
    }

    public static function setArrayBool(int entityId, string componentName, string fieldName, int index, bool value): bool {
        return _plugin_setArrayElement(entityId, componentName, fieldName, index, value);
    }

    // Append an element to the end of an array field
    public static function addArrayInt(int entityId, string componentName, string fieldName, int value): bool {
        return _plugin_addArrayElement(entityId, componentName, fieldName, value);
    }

    public static function addArrayFloat(int entityId, string componentName, string fieldName, float value): bool {
        return _plugin_addArrayElement(entityId, componentName, fieldName, value);
    }

    public static function addArrayString(int entityId, string componentName, string fieldName, string value): bool {
        return _plugin_addArrayElement(entityId, componentName, fieldName, value);
    }

    public static function addArrayBool(int entityId, string componentName, string fieldName, bool value): bool {
        return _plugin_addArrayElement(entityId, componentName, fieldName, value);
    }

    // Remove an element from an array field by index
    public static function removeArrayElement(int entityId, string componentName, string fieldName, int index): bool {
        return _plugin_removeArrayElement(entityId, componentName, fieldName, index);
    }

    // ============================================
    // Map Operations (std::map<string, T> fields)
    // ============================================

    // Get the number of entries in a map field
    public static function getMapSize(int entityId, string componentName, string fieldName): int {
        return _plugin_getMapSize(entityId, componentName, fieldName);
    }

    // Get all keys from a map field as a string array
    public static function getMapKeys(int entityId, string componentName, string fieldName): string[] {
        return _plugin_getMapKeys(entityId, componentName, fieldName);
    }

    // Get a typed value from a map field by key
    public static function getMapInt(int entityId, string componentName, string fieldName, string key): int {
        return (int)_plugin_getMapValue(entityId, componentName, fieldName, key);
    }

    public static function getMapFloat(int entityId, string componentName, string fieldName, string key): float {
        return (float)_plugin_getMapValue(entityId, componentName, fieldName, key);
    }

    public static function getMapString(int entityId, string componentName, string fieldName, string key): string {
        return (string)_plugin_getMapValue(entityId, componentName, fieldName, key);
    }

    public static function getMapBool(int entityId, string componentName, string fieldName, string key): bool {
        return (bool)_plugin_getMapValue(entityId, componentName, fieldName, key);
    }

    // Set or insert a typed value in a map field by key
    public static function setMapInt(int entityId, string componentName, string fieldName, string key, int value): bool {
        return _plugin_setMapValue(entityId, componentName, fieldName, key, value);
    }

    public static function setMapFloat(int entityId, string componentName, string fieldName, string key, float value): bool {
        return _plugin_setMapValue(entityId, componentName, fieldName, key, value);
    }

    public static function setMapString(int entityId, string componentName, string fieldName, string key, string value): bool {
        return _plugin_setMapValue(entityId, componentName, fieldName, key, value);
    }

    public static function setMapBool(int entityId, string componentName, string fieldName, string key, bool value): bool {
        return _plugin_setMapValue(entityId, componentName, fieldName, key, value);
    }

    // Remove an entry from a map field by key
    public static function removeMapEntry(int entityId, string componentName, string fieldName, string key): bool {
        return _plugin_removeMapEntry(entityId, componentName, fieldName, key);
    }

    // ============================================
    // Enum Operations
    // ============================================

    // Get current enum value as a string name
    public static function getEnum(int entityId, string componentName, string fieldName): string {
        return _plugin_getEnum(entityId, componentName, fieldName);
    }

    // Set enum value by string name
    public static function setEnum(int entityId, string componentName, string fieldName, string value): bool {
        return _plugin_setEnum(entityId, componentName, fieldName, value);
    }

    // Get all possible enum value names as a string array
    public static function getEnumValues(int entityId, string componentName, string fieldName): string[] {
        return _plugin_getEnumValues(entityId, componentName, fieldName);
    }
}
