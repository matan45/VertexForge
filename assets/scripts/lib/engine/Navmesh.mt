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
    public static int FORMATION_GRID = 0;
    public static int FORMATION_LINE = 1;
    public static int FORMATION_WEDGE = 2;
    public static int FORMATION_COLUMN = 3;
    public static int FORMATION_BOX = 4;

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
        int count = (int)raw[0];
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
    public static function raycast(Vec3f fromVec, Vec3f to): float[] {
        return _native_navmesh_raycast(fromVec.x, fromVec.y, fromVec.z, to.x, to.y, to.z);
    }

    // Monotonic tile version, bumped whenever navmesh streaming loads, unloads
    // or rebakes a tile. Cached findPath() waypoint lists go stale when tiles
    // change underneath them — store the version next to the path and re-path
    // when it differs:
    //   if (Navmesh::getTileVersion() != this.pathVersion) { recomputePath(); }
    public static function getTileVersion(): int {
        return _native_navmesh_getTileVersion();
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

    // Move a generic set of NavmeshAgent entities as a coordinated group.
    // Returns a group handle for optional status/debug queries; 0 means rejected.
    public static function setGroupDestination(int[] entityIds, Vec3f target, float spacing, int formationKind): int {
        return _native_navmesh_setGroupDestination(entityIds, target.x, target.y, target.z, spacing, formationKind);
    }

    public static function isGroupArrived(int groupId): bool {
        return _native_navmesh_isGroupArrived(groupId);
    }

    public static function getGroupCorridor(int groupId): Vec3f[] {
        float[] raw = _native_navmesh_getGroupCorridor(groupId);
        int count = (int)raw[0];
        Vec3f[] points = new Vec3f[count];
        for (int i = 0; i < count; i = i + 1) {
            int base = 1 + i * 3;
            points[i] = new Vec3f(raw[base], raw[base + 1], raw[base + 2]);
        }
        return points;
    }

    // Stop an entity's NavmeshAgent from moving
    public static function stopAgent(int entityId): void {
        _native_navmesh_stopAgent(entityId);
    }

    // ============================================
    // Agent Configuration
    // ============================================
	// Enable/disable an entity's NavmeshObstacle at runtime. Disable it while a
    // building ghost follows the cursor (an active carve-obstacle re-bakes the
    // navmesh every frame and freezes nearby units); enable it on commit to
    // carve the placed building's footprint into the navmesh exactly once.
    public static function setObstacleActive(int entityId, bool active): void {
         _native_navmesh_setObstacleActive(entityId, active);
    }
    // Set the maximum speed for an entity's NavmeshAgent
    public static function setSpeed(int entityId, float speed): void {
        _native_navmesh_setAgentSpeed(entityId, speed);
    }

    // Set the maximum acceleration for an entity's NavmeshAgent
    public static function setAcceleration(int entityId, float accel): void {
        _native_navmesh_setAgentAcceleration(entityId, accel);
    }

    // Enable/disable root-motion-driven locomotion: the animation's root motion sets
    // the agent's ground speed while nav still paths/steers/avoids/stops (VK-1408).
    public static function setRootMotionDriven(int entityId, bool enabled): void {
        _native_navmesh_setRootMotionDriven(entityId, enabled);
    }

    // Multiplier on root-motion ground speed for a driven agent (1.0 = clip speed).
    public static function setRootMotionScale(int entityId, float scale): void {
        _native_navmesh_setRootMotionScale(entityId, scale);
    }

    // Max turn rate in degrees/second for facing the travel direction. 0 = instant snap;
    // >0 eases the unit toward its heading so it doesn't "jump" forward on sharp turns.
    public static function setTurnSpeed(int entityId, float degPerSec): void {
        _native_navmesh_setTurnSpeed(entityId, degPerSec);
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
