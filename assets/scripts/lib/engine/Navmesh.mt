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

    // Navmesh raycast for line-of-sight checks
    // Returns float[4]: [hit(0/1), hitX, hitY, hitZ]
    // If hit is 0, the path is clear (no obstacle between from and to on the navmesh)
    public static function raycast(Vec3f from, Vec3f to): float[] {
        return _native_navmesh_raycast(from.x, from.y, from.z, to.x, to.y, to.z);
    }

    // ============================================
    // Agent Control
    // ============================================

    // Set the navigation target for an entity's NavmeshAgent
    // The agent will pathfind and move toward the target with crowd avoidance
    public static function setDestination(int entityId, Vec3f target): void {
        _native_navmesh_setDestination(entityId, target.x, target.y, target.z);
    }

    // High-level move-to command (alias for setDestination)
    public static function moveTo(int entityId, Vec3f target): void {
        _native_navmesh_setDestination(entityId, target.x, target.y, target.z);
    }

    // Stop an entity's NavmeshAgent from moving
    public static function stopAgent(int entityId): void {
        _native_navmesh_stopAgent(entityId);
    }

    // ============================================
    // Agent Configuration
    // ============================================

    // Set the maximum speed for an entity's NavmeshAgent
    public static function setSpeed(int entityId, float speed): void {
        _native_navmesh_setAgentSpeed(entityId, speed);
    }

    // Set the maximum acceleration for an entity's NavmeshAgent
    public static function setAcceleration(int entityId, float accel): void {
        _native_navmesh_setAgentAcceleration(entityId, accel);
    }

    // Get the current max speed of an entity's NavmeshAgent
    public static function getSpeed(int entityId): float {
        return _native_navmesh_getAgentSpeed(entityId);
    }

    // Get the current velocity of an entity's NavmeshAgent
    public static function getVelocity(int entityId): Vec3f {
        float[] raw = _native_navmesh_getAgentVelocity(entityId);
        return new Vec3f(raw[0], raw[1], raw[2]);
    }
}
