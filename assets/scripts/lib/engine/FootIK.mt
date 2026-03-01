// FootIK - Static utility class for foot IK calculations
// Calculates foot placement targets for terrain adaptation and pelvis offset
//
// Usage example:
//   import * from "../engine/FootIK.mt";
//   import * from "../math/Vec3f.mt";
//
//   // Calculate foot target from a physics raycast result
//   FootIKResult result = FootIK.calculateFootTarget(
//       footWorldPos,           // current foot bone world position
//       Vec3f.up(),             // character up vector
//       true,                   // raycast hit
//       hitPoint,               // raycast hit point
//       hitNormal,              // surface normal at hit
//       hitDistance,             // raycast distance
//       0.05,                   // foot sole height offset
//       0.5                     // max step height before IK disengages
//   );
//
//   if (result.isGrounded) {
//       IK.setTarget(self, "LeftFoot", result.targetPosition);
//       IK.setChainWeight(self, "LeftFoot", result.weight);
//   }
//
//   // Calculate pelvis offset to prevent leg over-extension
//   float pelvisY = FootIK.calculatePelvisOffset(
//       leftResult, leftFootAnimatedPos.y,
//       rightResult, rightFootAnimatedPos.y,
//       currentOffset, deltaTime, 5.0
//   );

import * from "../math/Vec3f.mt";
import * from "../math/Quaternion.mt";

public class FootIKResult {
    public Vec3f targetPosition;
    public Quaternion targetRotation;
    public float weight;
    public bool isGrounded;

    public constructor(Vec3f pos, Quaternion rot, float w, bool grounded) {
        this.targetPosition = pos;
        this.targetRotation = rot;
        this.weight = w;
        this.isGrounded = grounded;
    }

    public constructor() {
        this.targetPosition = Vec3f.zero();
        this.targetRotation = Quaternion.identity();
        this.weight = 0.0;
        this.isGrounded = false;
    }
}

public class FootIK {
    public constructor() {
    }

    // Calculate IK target for a single foot given its world position and a downward raycast result.
    // footHeight: height of foot sole above ground contact (default 0.05)
    // maxStepHeight: max terrain deviation before IK disengages (default 0.5)
    public static function calculateFootTarget(
        Vec3f footWorldPos,
        Vec3f characterUp,
        bool hit,
        Vec3f hitPoint,
        Vec3f hitNormal,
        float hitDistance,
        float footHeight,
        float maxStepHeight
    ): FootIKResult {
        float[] r = _native_footik_calculateFootTarget(
            footWorldPos.x, footWorldPos.y, footWorldPos.z,
            characterUp.x, characterUp.y, characterUp.z,
            hit,
            hitPoint.x, hitPoint.y, hitPoint.z,
            hitNormal.x, hitNormal.y, hitNormal.z,
            hitDistance,
            footHeight, maxStepHeight
        );

        return new FootIKResult(
            new Vec3f(r[0], r[1], r[2]),
            new Quaternion(r[3], r[4], r[5], r[6]),
            r[7],
            r[8] > 0.5
        );
    }

    // Calculate pelvis Y-offset to prevent leg over-extension.
    // leftOriginalY/rightOriginalY: animated foot Y positions before IK.
    // adjustSpeed: how fast the pelvis adjusts (default 5.0)
    public static function calculatePelvisOffset(
        FootIKResult leftFoot,
        float leftOriginalY,
        FootIKResult rightFoot,
        float rightOriginalY,
        float currentOffset,
        float deltaTime,
        float adjustSpeed
    ): float {
        return _native_footik_calculatePelvisOffset(
            leftFoot.targetPosition.x, leftFoot.targetPosition.y, leftFoot.targetPosition.z,
            leftFoot.weight, leftFoot.isGrounded, leftOriginalY,
            rightFoot.targetPosition.x, rightFoot.targetPosition.y, rightFoot.targetPosition.z,
            rightFoot.weight, rightFoot.isGrounded, rightOriginalY,
            currentOffset, deltaTime, adjustSpeed
        );
    }
}
