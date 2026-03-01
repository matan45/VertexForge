// HandIK - Static utility class for hand/arm IK calculations
// Calculates hand reach targets and two-handed weapon grips
//
// Usage example:
//   import * from "../engine/HandIK.mt";
//   import * from "../math/Vec3f.mt";
//   import * from "../math/Quaternion.mt";
//
//   // Calculate hand target for reaching an object
//   HandIKResult result = HandIK.calculateHandTarget(
//       handWorldPos,           // current hand bone world position
//       shoulderWorldPos,       // shoulder/arm root world position
//       objectPos,              // target position to reach
//       null,                   // optional target rotation (Quaternion or null)
//       1.5,                    // max reach distance
//       1.0                     // grip rotation blend (0=keep anim, 1=match target)
//   );
//
//   if (result.isReachable) {
//       IK.setTarget(self, "RightArm", result.targetPosition);
//       IK.setChainWeight(self, "RightArm", result.weight);
//   }
//
//   // Two-handed weapon grip: compute off-hand position from dominant hand
//   HandIKResult offHand = HandIK.calculateTwoHandedGrip(
//       domHandPos, domHandRot, // dominant hand world transform
//       new Vec3f(0.0, 0.1, -0.15),  // grip offset in dominant hand space
//       leftShoulderPos,        // off-hand shoulder position
//       1.5, 1.0                // maxReach, gripRotationBlend
//   );

import * from "../math/Vec3f.mt";
import * from "../math/Quaternion.mt";

public class HandIKResult {
    public Vec3f targetPosition;
    public Quaternion targetRotation;
    public float weight;
    public bool isReachable;

    public constructor(Vec3f pos, Quaternion rot, float w, bool reachable) {
        this.targetPosition = pos;
        this.targetRotation = rot;
        this.weight = w;
        this.isReachable = reachable;
    }

    public constructor() {
        this.targetPosition = Vec3f.zero();
        this.targetRotation = Quaternion.identity();
        this.weight = 0.0;
        this.isReachable = false;
    }
}

public class HandIK {
    public constructor() {
    }

    // Calculate IK target for a hand/arm chain to reach a world position.
    // targetRotation can be null to skip rotation matching.
    // maxReachDistance: max distance hand can reach (default 1.5)
    // gripRotationBlend: 0=keep animation rotation, 1=fully match target (default 1.0)
    public static function calculateHandTarget(
        Vec3f handWorldPos,
        Vec3f shoulderWorldPos,
        Vec3f targetWorldPos,
        Quaternion targetRotation,
        float maxReachDistance,
        float gripRotationBlend
    ): HandIKResult {
        bool hasRot = targetRotation != null;
        float rotX = 0.0;
        float rotY = 0.0;
        float rotZ = 0.0;
        float rotW = 1.0;
        if (hasRot) {
            rotX = targetRotation.x;
            rotY = targetRotation.y;
            rotZ = targetRotation.z;
            rotW = targetRotation.w;
        }

        float[] r = _native_handik_calculateHandTarget(
            handWorldPos.x, handWorldPos.y, handWorldPos.z,
            shoulderWorldPos.x, shoulderWorldPos.y, shoulderWorldPos.z,
            targetWorldPos.x, targetWorldPos.y, targetWorldPos.z,
            hasRot, rotX, rotY, rotZ, rotW,
            maxReachDistance, gripRotationBlend
        );

        return new HandIKResult(
            new Vec3f(r[0], r[1], r[2]),
            new Quaternion(r[3], r[4], r[5], r[6]),
            r[7],
            r[8] > 0.5
        );
    }

    // Calculate two-handed weapon grip position for the off-hand.
    // Given the dominant hand transform and a grip offset in dominant-hand local space,
    // returns the IK target for the off-hand.
    public static function calculateTwoHandedGrip(
        Vec3f dominantHandPos,
        Quaternion dominantHandRot,
        Vec3f gripOffset,
        Vec3f offHandShoulderPos,
        float maxReachDistance,
        float gripRotationBlend
    ): HandIKResult {
        float[] r = _native_handik_calculateTwoHandedGrip(
            dominantHandPos.x, dominantHandPos.y, dominantHandPos.z,
            dominantHandRot.x, dominantHandRot.y, dominantHandRot.z, dominantHandRot.w,
            gripOffset.x, gripOffset.y, gripOffset.z,
            offHandShoulderPos.x, offHandShoulderPos.y, offHandShoulderPos.z,
            maxReachDistance, gripRotationBlend
        );

        return new HandIKResult(
            new Vec3f(r[0], r[1], r[2]),
            new Quaternion(r[3], r[4], r[5], r[6]),
            r[7],
            r[8] > 0.5
        );
    }
}
