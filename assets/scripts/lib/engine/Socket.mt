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
//       float[] pos = Socket::getPosition(character, "LeftHand");
//   }
//
//   // Detach
//   Socket::detach(self);

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

    // Get the world position of a socket as float[3] (x, y, z)
    public static function getPosition(int parentEntityId, string socketName): float[] {
        return _native_socket_getPosition(parentEntityId, socketName);
    }

    // Get the world rotation of a socket as float[4] (w, x, y, z quaternion)
    public static function getRotation(int parentEntityId, string socketName): float[] {
        return _native_socket_getRotation(parentEntityId, socketName);
    }

    // Get the full world transform of a socket as float[16] (4x4 matrix, column-major)
    public static function getTransform(int parentEntityId, string socketName): float[] {
        return _native_socket_getTransform(parentEntityId, socketName);
    }
}
