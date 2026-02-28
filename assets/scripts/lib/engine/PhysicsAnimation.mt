// PhysicsAnimation - Static utility class for ragdoll and kinematic bone physics
// Works with entity IDs (int) to control physics-driven animation
//
// Requires the entity to have a PhysicsAnimationComponent configured with bone body mappings.
//
// Usage examples:
//   int self = Entity::self();
//
//   // Check if entity has physics animation
//   if (PhysicsAnimation::hasPhysicsAnimation(self)) {
//
//       // Activate ragdoll (e.g., on death)
//       PhysicsAnimation::activateRagdoll(self);
//
//       // Activate ragdoll with an impulse (e.g., hit by explosion)
//       PhysicsAnimation::activateRagdollWithImpulse(self, new Vec3f(0.0, 500.0, 0.0));
//
//       // Activate ragdoll with impulse on a specific bone
//       PhysicsAnimation::activateRagdollAtBone(self, new Vec3f(0.0, 500.0, 0.0), boneIndex);
//
//       // Apply impulse to an active ragdoll
//       PhysicsAnimation::applyRagdollImpulse(self, new Vec3f(100.0, 0.0, 0.0));
//
//       // Apply impulse to a specific bone
//       PhysicsAnimation::applyRagdollBoneImpulse(self, 3, new Vec3f(100.0, 0.0, 0.0));
//
//       // Deactivate ragdoll (return to animation)
//       PhysicsAnimation::deactivateRagdoll(self);
//   }

import * from "../math/Vec3f.mt";

public class PhysicsAnimation {
    // ============================================
    // Mode Constants
    // ============================================
    public static const int MODE_ANIMATED = 0;
    public static const int MODE_KINEMATIC = 1;
    public static const int MODE_RAGDOLL = 2;

    public constructor() {
    }

    // ============================================
    // Queries
    // ============================================

    // Check if entity has a PhysicsAnimationComponent
    public static function hasPhysicsAnimation(int entityId): bool {
        return _native_physanim_hasPhysicsAnimation(entityId);
    }

    // Check if the ragdoll is currently active
    public static function isRagdollActive(int entityId): bool {
        return _native_physanim_isRagdollActive(entityId);
    }

    // Get current mode (MODE_ANIMATED, MODE_KINEMATIC, MODE_RAGDOLL)
    public static function getMode(int entityId): int {
        return _native_physanim_getMode(entityId);
    }

    // ============================================
    // Ragdoll Control
    // ============================================

    // Activate ragdoll mode (physics takes over bone transforms)
    // The animator will be paused automatically
    public static function activateRagdoll(int entityId): void {
        _native_physanim_activateRagdoll(entityId);
    }

    // Activate ragdoll with an initial impulse applied to all bodies
    public static function activateRagdollWithImpulse(int entityId, Vec3f impulse): void {
        _native_physanim_activateRagdoll(entityId, impulse.x, impulse.y, impulse.z);
    }

    // Activate ragdoll with an impulse on a specific bone
    // boneIndex is the animation bone index from the skeleton
    public static function activateRagdollAtBone(int entityId, Vec3f impulse, int boneIndex): void {
        _native_physanim_activateRagdoll(entityId, impulse.x, impulse.y, impulse.z, boneIndex);
    }

    // Deactivate ragdoll and return to animation-driven mode
    // The animator will resume playback automatically
    public static function deactivateRagdoll(int entityId): void {
        _native_physanim_deactivateRagdoll(entityId);
    }

    // ============================================
    // Ragdoll Impulse
    // ============================================

    // Apply an impulse to the entire ragdoll (all bodies)
    // Only works when ragdoll is active
    public static function applyRagdollImpulse(int entityId, Vec3f impulse): void {
        _native_physanim_applyRagdollImpulse(entityId, impulse.x, impulse.y, impulse.z);
    }

    // Apply an impulse to a specific ragdoll bone
    // boneIndex is the animation bone index from the skeleton
    // Only works when ragdoll is active
    public static function applyRagdollBoneImpulse(int entityId, int boneIndex, Vec3f impulse): void {
        _native_physanim_applyRagdollBoneImpulse(entityId, boneIndex, impulse.x, impulse.y, impulse.z);
    }
}
