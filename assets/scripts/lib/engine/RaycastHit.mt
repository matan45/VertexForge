// RaycastHit - Holds the result of a single raycast hit
//
// Usage:
//   RaycastHit hit = Physics::raycast(origin, direction, 10.0);
//   if (hit.hit) {
//       Log::info("Hit entity " + hit.entityId + " at distance " + hit.distance);
//   }
//
//   RaycastHit[] hits = Physics::raycastAll(origin, direction, 10.0);
//   for (int i = 0; i < hits.length(); i = i + 1) {
//       Log::info("Hit entity " + hits[i].entityId);
//   }

import * from "../math/Vec3f.mt";

public class RaycastHit {
    public bool hit = false;
    public int entityId = -1;
    public Vec3f point = new Vec3f(0.0, 0.0, 0.0);
    public Vec3f normal = new Vec3f(0.0, 0.0, 0.0);
    public float distance = 0.0;

    public constructor() {
    }

    public constructor(bool hit, int entityId, Vec3f point, Vec3f normal, float distance) {
        this.hit = hit;
        this.entityId = entityId;
        this.point = point;
        this.normal = normal;
        this.distance = distance;
    }
}
