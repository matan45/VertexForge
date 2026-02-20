// Navmesh - Static utility class for navigation mesh pathfinding
// Provides pathfinding queries and agent control for navmesh-based navigation
//
// Usage examples:
//   int self = Entity::self();
//   Navmesh::setDestination(self, 10.0, 0.0, 5.0);  // Move agent to target
//   Navmesh::stopAgent(self);                         // Stop agent movement
//   float[] path = Navmesh::findPath(0.0, 0.0, 0.0, 10.0, 0.0, 5.0);
//   bool onMesh = Navmesh::isPointOnNavmesh(5.0, 0.0, 3.0);
//
// Note: Agents must have a NavmeshAgent component and the navmesh must be baked
// before pathfinding functions will work. Use the Navigation window in the editor
// to bake the navmesh.

public class Navmesh {

    public constructor() {
    }

    // ============================================
    // Pathfinding
    // ============================================

    // Find a path between two world positions
    // Returns float[] with format: [waypointCount, x0, y0, z0, x1, y1, z1, ...]
    // Returns [0] if no path found
    // Rate limited: max 50 path queries per frame
    public static function findPath(float sx, float sy, float sz, float ex, float ey, float ez): float[] {
        return _native_navmesh_findPath(sx, sy, sz, ex, ey, ez);
    }

    // Check if a world position is on the navmesh
    public static function isPointOnNavmesh(float x, float y, float z): bool {
        return _native_navmesh_isPointOnNavmesh(x, y, z);
    }

    // Get the closest point on the navmesh to a world position
    // Returns float[3] (x, y, z)
    public static function getClosestPoint(float x, float y, float z): float[] {
        return _native_navmesh_getClosestPoint(x, y, z);
    }

    // ============================================
    // Agent Control
    // ============================================

    // Set the navigation target for an entity's NavmeshAgent
    // The agent will pathfind and move toward the target with crowd avoidance
    public static function setDestination(int entityId, float tx, float ty, float tz): void {
        _native_navmesh_setDestination(entityId, tx, ty, tz);
    }

    // Stop an entity's NavmeshAgent from moving
    public static function stopAgent(int entityId): void {
        _native_navmesh_stopAgent(entityId);
    }
}
