// EQS - Static utility class for Environment Query System
// Provides spatial AI decision-making queries (find cover, flanking positions, etc.)
//
// Usage examples:
//   int self = Entity::self();
//   Vec3f pos = Transform::getPosition(self);
//   Vec3f fwd = Transform::getForward(self);
//   int handle = EQS::submitQuery("FindCover", self, pos, fwd);
//   // ... later, poll for results:
//   float[] result = EQS::getResult(handle);
//   if (result[0] == 2.0) { // status 2 = Completed
//       Vec3f bestPos = new Vec3f(result[1], result[2], result[3]);
//       Navmesh::setDestination(self, bestPos);
//   }
//
// Note: Queries must be registered by name before use (typically in game setup code).
// Status values: 0=Pending, 1=Running, 2=Completed, 3=Failed

import * from "../math/Vec3f.mt";

public class EQS {

    // Status constants
    public static int PENDING = 0;
    public static int RUNNING = 1;
    public static int COMPLETED = 2;
    public static int FAILED = 3;

    public constructor() {
    }

    // ============================================
    // Query Submission
    // ============================================

    // Submit an EQS query by name
    // Returns a query handle (int) for polling results
    // querierEntityId: the entity performing the query
    // position: world position of the querier
    // forward: forward direction (XZ only, Y ignored)
    public static function submitQuery(string queryName, int querierEntityId,
                                        Vec3f position, Vec3f forward): int {
        return toInt(_native_eqs_submitQuery(queryName, querierEntityId,
            position.x, position.y, position.z, forward.x, forward.z));
    }

    // ============================================
    // Result Polling
    // ============================================

    // Get the result of a previously submitted query
    // Returns float[5]: [status, bestX, bestY, bestZ, bestScore]
    // Status: 0=Pending, 1=Running, 2=Completed, 3=Failed
    public static function getResult(int handle): float[] {
        return _native_eqs_getResult(handle);
    }

    // Get the best position from a completed query result
    // Returns Vec3f(0,0,0) if not completed or no results
    public static function getBestPosition(int handle): Vec3f {
        float[] raw = _native_eqs_getResult(handle);
        if (raw[0] == 2.0) {
            return new Vec3f(raw[1], raw[2], raw[3]);
        }
        return new Vec3f(0.0, 0.0, 0.0);
    }

    // Check if a query is completed
    public static function isCompleted(int handle): bool {
        float[] raw = _native_eqs_getResult(handle);
        return raw[0] == 2.0;
    }

    // ============================================
    // Query Management
    // ============================================

    // Cancel a pending or running query
    public static function cancelQuery(int handle): void {
        _native_eqs_cancelQuery(handle);
    }
}
