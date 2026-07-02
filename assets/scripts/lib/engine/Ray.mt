// Ray - A world-space ray (origin + normalized direction).
//
// Usage:
//   Ray? ray = Picker::screenToWorldRay(Input::getViewportMouseX(), Input::getViewportMouseY());
//   if (ray != null) {
//       RaycastHit hit = Physics::raycastHit(ray.origin, ray.direction, 100.0);
//       Vec3f probe = ray.getPoint(5.0);   // 5 units along the ray
//   }

import * from "../math/Vec3f.mt";

public class Ray {
    public Vec3f origin = new Vec3f(0.0, 0.0, 0.0);
    public Vec3f direction = new Vec3f(0.0, 0.0, 1.0);

    public constructor() {
    }

    public constructor(Vec3f origin, Vec3f direction) {
        this.origin = origin;
        this.direction = direction;
    }

    // Point at `distance` units along the ray: origin + direction * distance
    public function getPoint(float distance): Vec3f {
        return this.origin.add(this.direction.multiply(distance));
    }
}
