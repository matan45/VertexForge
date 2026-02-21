// Socket - Static utility class for socket attachment operations
// Sockets are named attachment points on skeleton bones (e.g., "RightHand", "Head")
// Use this to attach entities (weapons, shields, VFX) to animated characters
//
// Usage examples:
//   int self = Entity::self();
//   int character = Entity::findByName("Character");
//
//   // Attach a weapon to the character's right hand socket
//   Socket::attach(self, character, "RightHand");
//
//   // Check if attached
//   if (Socket::isAttached(self)) {
//       int parent = Socket::getParentEntity(self);
//   }
//
//   // Query socket info on the character
//   string[] sockets = Socket::getSockets(character);
//   if (Socket::hasSocket(character, "LeftHand")) {
//       Vec3f pos = Socket::getPosition(character, "LeftHand");
//   }
//
//   // Detach
//   Socket::detach(self);

import * from "../math/Vec3f.mt";
import * from "../math/Quaternion.mt";
import * from "../math/Matrix4f.mt";

public class Socket {
    public constructor() {
    }

    // ============================================
    // Attachment Operations
    // ============================================

    // Attach a child entity to a parent entity's socket
    // The child's transform will follow the socket position each frame
    // Returns true if attachment succeeded
    public static function attach(int childEntityId, int parentEntityId, string socketName): bool {
        return _native_socket_attach(childEntityId, parentEntityId, socketName);
    }

    // Detach an entity from its current socket
    public static function detach(int entityId): void {
        _native_socket_detach(entityId);
    }

    // Enable or disable a socket attachment without detaching
    // When inactive, the entity stops following the socket but remains attached
    public static function setActive(int entityId, bool active): void {
        _native_socket_setActive(entityId, active);
    }

    // ============================================
    // Attachment Queries
    // ============================================

    // Check if an entity is currently attached to a socket
    public static function isAttached(int entityId): bool {
        return _native_socket_isAttached(entityId);
    }

    // Get the parent entity ID of an attached entity
    // Returns -1 if not attached
    public static function getParentEntity(int entityId): int {
        return _native_socket_getParentEntity(entityId);
    }

    // ============================================
    // Socket Queries
    // ============================================

    // Check if a parent entity has a socket with the given name
    public static function hasSocket(int parentEntityId, string socketName): bool {
        return _native_socket_hasSocket(parentEntityId, socketName);
    }

    // Get all socket names on a parent entity
    public static function getSockets(int parentEntityId): string[] {
        return _native_socket_getSockets(parentEntityId);
    }

    // Get the world position of a socket
    public static function getPosition(int parentEntityId, string socketName): Vec3f {
        float[] raw = _native_socket_getPosition(parentEntityId, socketName);
        return new Vec3f(raw[0], raw[1], raw[2]);
    }

    // Get the world rotation of a socket
    public static function getRotation(int parentEntityId, string socketName): Quaternion {
        // Native returns [w, x, y, z]
        float[] raw = _native_socket_getRotation(parentEntityId, socketName);
        return new Quaternion(raw[1], raw[2], raw[3], raw[0]);
    }

    // Get the full world transform of a socket
    // Native returns column-major float[16], converted to row-major Matrix4f
    public static function getTransform(int parentEntityId, string socketName): Matrix4f {
        float[] c = _native_socket_getTransform(parentEntityId, socketName);
        // Column-major [c0,c1,c2,c3, c4,c5,c6,c7, c8,c9,c10,c11, c12,c13,c14,c15]
        // to row-major Matrix4f(row0, row1, row2, row3)
        return new Matrix4f(
            c[0], c[4], c[8],  c[12],
            c[1], c[5], c[9],  c[13],
            c[2], c[6], c[10], c[14],
            c[3], c[7], c[11], c[15]
        );
    }
}
