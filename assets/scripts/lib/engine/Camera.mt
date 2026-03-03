// Camera - Static utility class for camera entity control
// Works with entity IDs (int) to manipulate camera properties
//
// Usage examples:
//   int cam = Entity::findByName("MainCamera");
//   Camera::setPosition(cam, new Vec3f(0.0, 10.0, -5.0));
//   Camera::setFOV(cam, 60.0);
//   Matrix4f view = Camera::getViewMatrix(cam);
//   int primary = Camera::getPrimary();

import * from "../math/Vec3f.mt";
import * from "../math/Matrix4f.mt";

public class Camera {
    public constructor() {
    }

    // ============================================
    // Position & Rotation
    // ============================================

    // Get camera entity position as Vec3f
    public static function getPosition(int entityId): Vec3f {
        float[] pos = _native_camera_getPosition(entityId);
        return new Vec3f(pos[0], pos[1], pos[2]);
    }

    // Set camera entity position
    public static function setPosition(int entityId, Vec3f position): void {
        _native_camera_setPosition(entityId, position.x, position.y, position.z);
    }

    // Get camera entity rotation as Vec3f (Euler angles in degrees)
    public static function getRotation(int entityId): Vec3f {
        float[] rot = _native_camera_getRotation(entityId);
        return new Vec3f(rot[0], rot[1], rot[2]);
    }

    // Set camera entity rotation (Euler angles in degrees: pitch, yaw, roll)
    public static function setRotation(int entityId, Vec3f rotation): void {
        _native_camera_setRotation(entityId, rotation.x, rotation.y, rotation.z);
    }

    // ============================================
    // Camera Properties
    // ============================================

    // Get field of view in degrees
    public static function getFOV(int entityId): float {
        return _native_camera_getFOV(entityId);
    }

    // Set field of view in degrees
    public static function setFOV(int entityId, float fov): void {
        _native_camera_setFOV(entityId, fov);
    }

    // Get near clipping plane distance
    public static function getNearPlane(int entityId): float {
        return _native_camera_getNearPlane(entityId);
    }

    // Set near clipping plane distance
    public static function setNearPlane(int entityId, float near): void {
        _native_camera_setNearPlane(entityId, near);
    }

    // Get far clipping plane distance
    public static function getFarPlane(int entityId): float {
        return _native_camera_getFarPlane(entityId);
    }

    // Set far clipping plane distance
    public static function setFarPlane(int entityId, float far): void {
        _native_camera_setFarPlane(entityId, far);
    }

    // ============================================
    // Matrices
    // ============================================

    // Get the current view matrix (row-major Matrix4f)
    public static function getViewMatrix(int entityId): Matrix4f {
        float[] m = _native_camera_getViewMatrix(entityId);
        return new Matrix4f(
            m[0],  m[1],  m[2],  m[3],
            m[4],  m[5],  m[6],  m[7],
            m[8],  m[9],  m[10], m[11],
            m[12], m[13], m[14], m[15]
        );
    }

    // Get the current projection matrix (row-major Matrix4f)
    public static function getProjectionMatrix(int entityId): Matrix4f {
        float[] m = _native_camera_getProjectionMatrix(entityId);
        return new Matrix4f(
            m[0],  m[1],  m[2],  m[3],
            m[4],  m[5],  m[6],  m[7],
            m[8],  m[9],  m[10], m[11],
            m[12], m[13], m[14], m[15]
        );
    }

    // ============================================
    // Primary Camera
    // ============================================

    // Get the primary camera entity ID, returns -1 if none
    public static function getPrimary(): int {
        return _native_camera_getPrimary();
    }

    // Set whether this camera is the primary camera
    public static function setIsPrimary(int entityId, bool isPrimary): void {
        _native_camera_setIsPrimary(entityId, isPrimary);
    }
}
