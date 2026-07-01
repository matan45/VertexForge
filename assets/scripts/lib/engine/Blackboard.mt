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

    // Attach a behavior tree (.vfBehaviorTree) to an entity at runtime and return
    // whether it loaded. Use for entities spawned during play (e.g. units produced
    // from a building) that were not present at the Edit->Play transition. After
    // attaching, seed blackboard keys and call setEnabled(id, true) to start ticking.
    public static function attachTree(int entityId, string treePath): bool {
        return _native_bt_attachTree(entityId, treePath);
    }

    // Detach (and stop) the entity's behavior tree, clearing its runtime + blackboard.
    public static function detachTree(int entityId): void {
        _native_bt_detachTree(entityId);
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

    // === Dynamic subtrees (VK-1457) ===

    // Bind a DynamicSubTree injection tag to a tree path at runtime, so a "Run Dynamic Subtree" node
    // carrying that tag will run the given .vfBehaviorTree. Pass an empty path to clear the binding.
    // A DynamicSubTree resolves its target in this order: blackboard selection key > injection tag >
    // authoring-time default path. Use this to swap an agent's behavior (e.g. combat vs. patrol brain)
    // without editing the tree.
    public static function setDynamicSubtree(int entityId, string tag, string treePath): void {
        _native_bt_setDynamicSubtree(entityId, tag, treePath);
    }
}

// ============================================================================
// ScriptTask lifecycle (VK-1457) — a @Script used as a behavior-tree ScriptTask node may implement:
//   onStart()            called once, lazily, the first time the task is loaded for an entity
//   tick(float): string  called every Running tick; return "running" | "success" | "failure"
//   onAbort()            called when the running task is preempted/aborted by the tree
//   onEnd(bool success)  called when the task completes naturally (returns "success" or "failure")
//   onDestroy()          called when the tree is detached / play stops (instance unloaded)
// Instances are shared per (entity, scriptPath): onStart fires once per load (not per activation), and
// two ScriptTask nodes referencing the same script on one entity share a single instance.
// ============================================================================
