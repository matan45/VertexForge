// Blackboard - Static utility class for accessing behavior tree blackboard data
// Used within ScriptTask tick() methods to read/write shared AI state

public class Blackboard {
    public constructor() {
    }

    // === Setters ===

    public static function setFloat(int entityId, string key, float value): void {
        _native_bt_setBlackboardFloat(entityId, key, value);
    }

    public static function setInt(int entityId, string key, int value): void {
        _native_bt_setBlackboardInt(entityId, key, value);
    }

    public static function setBool(int entityId, string key, bool value): void {
        _native_bt_setBlackboardBool(entityId, key, value);
    }

    public static function setString(int entityId, string key, string value): void {
        _native_bt_setBlackboardString(entityId, key, value);
    }

    public static function setVec3(int entityId, string key, float x, float y, float z): void {
        _native_bt_setBlackboardVec3(entityId, key, x, y, z);
    }

    // === Getters ===

    public static function getFloat(int entityId, string key): float {
        return _native_bt_getBlackboardFloat(entityId, key);
    }

    public static function getInt(int entityId, string key): int {
        return _native_bt_getBlackboardInt(entityId, key);
    }

    public static function getBool(int entityId, string key): bool {
        return _native_bt_getBlackboardBool(entityId, key);
    }

    public static function getString(int entityId, string key): string {
        return _native_bt_getBlackboardString(entityId, key);
    }

    public static function getVec3(int entityId, string key): float[] {
        return _native_bt_getBlackboardVec3(entityId, key);
    }

    // === Utilities ===

    public static function hasKey(int entityId, string key): bool {
        return _native_bt_hasBlackboardKey(entityId, key);
    }

    public static function hasBehaviorTree(int entityId): bool {
        return _native_bt_hasBehaviorTree(entityId);
    }

    public static function setEnabled(int entityId, bool enabled): void {
        _native_bt_setBehaviorTreeEnabled(entityId, enabled);
    }

    // Check if a behavior tree is currently enabled
    public static function isEnabled(int entityId): bool {
        return _native_bt_isEnabled(entityId);
    }

    // Get the current status of a behavior tree: "running", "success", "failure", or "stopped"
    public static function getStatus(int entityId): string {
        return _native_bt_getStatus(entityId);
    }
}
