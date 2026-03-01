// IK - Static utility class for controlling Inverse Kinematics on entities
// Works with entity IDs (int) and chain names (string)
//
// Usage examples:
//   int self = Entity::self();
//
//   // Check if entity has IK component
//   if (IK::hasComponent(self)) {
//       // Set IK target for a chain
//       IK::setTarget(self, "LeftArm", new Vec3f(1.0, 2.0, 3.0));
//
//       // Set target with rotation
//       IK::setTargetWithRotation(self, "RightArm", new Vec3f(1.0, 2.0, 3.0), Quaternion::identity());
//
//       // Control chain weight (0.0 = animation only, 1.0 = full IK)
//       IK::setChainWeight(self, "LeftArm", 0.75);
//
//       // Enable/disable chains
//       IK::setChainEnabled(self, "LeftArm", true);
//
//       // Query chain state
//       float weight = IK::getChainWeight(self, "LeftArm");
//       bool enabled = IK::isChainEnabled(self, "LeftArm");
//   }

import * from "../math/Vec3f.mt";
import * from "../math/Quaternion.mt";

public class IK {
    public constructor() {
    }

    // ============================================
    // Component Queries
    // ============================================

    // Check if an entity has an IK component attached
    public static function hasComponent(int entityId): bool {
        return _native_ik_hasComponent(entityId);
    }

    // Get all chain names configured on an entity
    public static function getChainNames(int entityId): string[] {
        return _native_ik_getChainNames(entityId);
    }

    // ============================================
    // Target Control
    // ============================================

    // Set the IK target position for a chain
    public static function setTarget(int entityId, string chainName, Vec3f position): void {
        _native_ik_setTarget(entityId, chainName, position.x, position.y, position.z);
    }

    // Set the IK target position and rotation for a chain
    public static function setTargetWithRotation(int entityId, string chainName, Vec3f position, Quaternion rotation): void {
        _native_ik_setTargetWithRotation(entityId, chainName, position.x, position.y, position.z, rotation.w, rotation.x, rotation.y, rotation.z);
    }

    // ============================================
    // Chain Weight & Enabled
    // ============================================

    // Set the blend weight for a chain (0.0 = animation only, 1.0 = full IK)
    public static function setChainWeight(int entityId, string chainName, float weight): void {
        _native_ik_setChainWeight(entityId, chainName, weight);
    }

    // Get the blend weight for a chain
    public static function getChainWeight(int entityId, string chainName): float {
        return _native_ik_getChainWeight(entityId, chainName);
    }

    // Enable or disable a chain
    public static function setChainEnabled(int entityId, string chainName, bool enabled): void {
        _native_ik_setChainEnabled(entityId, chainName, enabled);
    }

    // Check if a chain is enabled
    public static function isChainEnabled(int entityId, string chainName): bool {
        return _native_ik_isChainEnabled(entityId, chainName);
    }
}
