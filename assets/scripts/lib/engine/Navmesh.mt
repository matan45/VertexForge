// Navmesh - Static utility class for navigation mesh pathfinding
// Provides pathfinding queries and agent control for navmesh-based navigation
//
// Usage examples:
//   int self = Entity::self();
//   Navmesh::setDestination(self, new Vec3f(10.0, 0.0, 5.0));
//   Navmesh::stopAgent(self);
//   Vec3f[] path = Navmesh::findPath(new Vec3f(0.0, 0.0, 0.0), new Vec3f(10.0, 0.0, 5.0));
//   bool onMesh = Navmesh::isPointOnNavmesh(new Vec3f(5.0, 0.0, 3.0));
//
// Note: Agents must have a NavmeshAgent component and the navmesh must be baked
// before pathfinding functions will work. Use the Navigation window in the editor
// to bake the navmesh.

import * from "../math/Vec3f.mt";

public class Navmesh {

    public constructor() {
    }

    // ============================================
    // Pathfinding
    // ============================================

    // Find a path between two world positions
    // Returns Vec3f[] of waypoints, empty array if no path found
    // Rate limited: max 50 path queries per frame
    public static function findPath(Vec3f start, Vec3f end): Vec3f[] {
        float[] raw = _native_navmesh_findPath(start.x, start.y, start.z, end.x, end.y, end.z);
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

    // Check if a world position is on the navmesh
    public static function isPointOnNavmesh(Vec3f point): bool {
        return _native_navmesh_isPointOnNavmesh(point.x, point.y, point.z);
    }

    // Get the closest point on the navmesh to a world position
    public static function getClosestPoint(Vec3f point): Vec3f {
        float[] raw = _native_navmesh_getClosestPoint(point.x, point.y, point.z);
        return new Vec3f(raw[0], raw[1], raw[2]);
    }

    // ============================================
    // Agent Control
    // ============================================

    // Set the navigation target for an entity's NavmeshAgent
    // The agent will pathfind and move toward the target with crowd avoidance
    public static function setDestination(int entityId, Vec3f target): void {
        _native_navmesh_setDestination(entityId, target.x, target.y, target.z);
    }

    // Stop an entity's NavmeshAgent from moving
    public static function stopAgent(int entityId): void {
        _native_navmesh_stopAgent(entityId);
    }
}
