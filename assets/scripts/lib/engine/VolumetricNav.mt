// VolumetricNav - Static utility class for 3D volumetric navigation (flying/swimming)
// Provides 3D pathfinding and agent control for volumetric navigation spaces
//
// Usage examples:
//   int self = Entity::self();
//   VolumetricNav::setDestination(self, new Vec3f(10.0, 20.0, 5.0));
//   VolumetricNav::stopAgent(self);
//   Vec3f[] path = VolumetricNav::findPath3D(new Vec3f(0.0, 5.0, 0.0), new Vec3f(10.0, 20.0, 5.0));
//   bool nav = VolumetricNav::isPointNavigable(new Vec3f(5.0, 10.0, 3.0));
//
// Note: Entities must have a VolumetricAgent component and a baked VolumetricNavVolume
// must exist in the scene for pathfinding to work.

import * from "../math/Vec3f.mt";

public class VolumetricNav {

    public constructor() {
    }

    // ============================================
    // 3D Pathfinding
    // ============================================

    // Find a 3D path between two world positions through volumetric space
    // Returns Vec3f[] of waypoints, empty array if no path found
    public static function findPath3D(Vec3f start, Vec3f end): Vec3f[] {
        float[] raw = _native_volumetric_findPath3D(start.x, start.y, start.z, end.x, end.y, end.z);
        int count = toInt(raw[0]);
        if (count <= 0) {
            return new Vec3f[0];
        }
        Vec3f[] waypoints = new Vec3f[count];
        for (int i = 0; i < count; i = i + 1) {
            int base = 1 + i * 3;
            waypoints[i] = new Vec3f(raw[base], raw[base + 1], raw[base + 2]);
        }
        return waypoints;
    }

    // Check if a 3D position is navigable in volumetric space
    public static function isPointNavigable(Vec3f point): bool {
        return _native_volumetric_isPointNavigable(point.x, point.y, point.z);
    }

    // ============================================
    // Agent Control
    // ============================================

    // Set the 3D navigation target for a VolumetricAgent
    public static function setDestination(int entityId, Vec3f target): void {
        _native_volumetric_setDestination(entityId, target.x, target.y, target.z);
    }

    // Stop a VolumetricAgent from moving
    public static function stopAgent(int entityId): void {
        _native_volumetric_stopAgent(entityId);
    }
}
