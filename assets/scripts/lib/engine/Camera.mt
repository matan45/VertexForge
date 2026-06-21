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

    // ============================================
    // Render-layer culling mask (VK-1415)
    // ============================================

    // 32-bit per-camera render-layer mask: a mesh on layer N is visible to this
    // camera only if bit N is set. 0xFFFFFFFF (-1 as int) = all layers (default).
    // Applies to the camera's view, including an RTT/minimap driven by it. Build a
    // mask with bit ops, e.g. all-except-layer-10: ~(1 << 10).
    public static function getCullingMask(int entityId): int {
        return _native_camera_getCullingMask(entityId);
    }

    public static function setCullingMask(int entityId, int mask): void {
        _native_camera_setCullingMask(entityId, mask);
    }

    // ============================================
    // View / Projection control (VK-1416)
    // ============================================

    // Aim the camera at a world-space point from its current position. Pins the view
    // (engine stops recomputing it each frame) until setViewMatrixOverride(id, false).
    public static function lookAt(int entityId, Vec3f target, Vec3f up): void {
        _native_camera_lookAt(entityId, target.x, target.y, target.z, up.x, up.y, up.z);
    }

    // Set an arbitrary view matrix (row-major). Pins the view (viewMatrixOverride = true).
    public static function setViewMatrix(int entityId, Matrix4f m): void {
        float[] a = new float[16];
        a[0]  = m.m00; a[1]  = m.m01; a[2]  = m.m02; a[3]  = m.m03;
        a[4]  = m.m10; a[5]  = m.m11; a[6]  = m.m12; a[7]  = m.m13;
        a[8]  = m.m20; a[9]  = m.m21; a[10] = m.m22; a[11] = m.m23;
        a[12] = m.m30; a[13] = m.m31; a[14] = m.m32; a[15] = m.m33;
        _native_camera_setViewMatrix(entityId, a);
    }

    // Set an arbitrary projection matrix (row-major). Pins the camera (viewMatrixOverride = true).
    public static function setProjectionMatrix(int entityId, Matrix4f m): void {
        float[] a = new float[16];
        a[0]  = m.m00; a[1]  = m.m01; a[2]  = m.m02; a[3]  = m.m03;
        a[4]  = m.m10; a[5]  = m.m11; a[6]  = m.m12; a[7]  = m.m13;
        a[8]  = m.m20; a[9]  = m.m21; a[10] = m.m22; a[11] = m.m23;
        a[12] = m.m30; a[13] = m.m31; a[14] = m.m32; a[15] = m.m33;
        _native_camera_setProjectionMatrix(entityId, a);
    }

    // Toggle whether the script owns the camera matrices. Pass false to return to engine-driven view.
    public static function setViewMatrixOverride(int entityId, bool enabled): void {
        _native_camera_setViewMatrixOverride(entityId, enabled);
    }

    public static function getViewMatrixOverride(int entityId): bool {
        return _native_camera_getViewMatrixOverride(entityId);
    }

    // ============================================
    // Projection mode (VK-1416)
    // ============================================

    // true = orthographic, false = perspective. Projection rebuilds immediately and keeps
    // tracking the window aspect ratio (unless a custom projection override is active).
    public static function setOrthographic(int entityId, bool orthographic): void {
        _native_camera_setOrthographic(entityId, orthographic);
    }

    public static function isOrthographic(int entityId): bool {
        return _native_camera_isOrthographic(entityId);
    }

    // Half-height of the orthographic view volume, in world units.
    public static function setOrthoSize(int entityId, float size): void {
        _native_camera_setOrthoSize(entityId, size);
    }

    public static function getOrthoSize(int entityId): float {
        return _native_camera_getOrthoSize(entityId);
    }

    // ============================================
    // Render-to-texture (VK-1417)
    // ============================================

    // One-call RTT setup on the camera's OWN entity. Prerequisite: the camera entity carries a
    // RenderTextureComponent (added in the editor) which the engine materializes at play entry.
    // The RTT auto-binds to this entity's camera; consume it by this entity id from UI/material.
    // updateMode: use RenderTextureUpdateMode constants (EVERY_FRAME=0, ON_DEMAND=1, FIXED_INTERVAL=2).
    public static function renderTo(int cameraEntityId, int width, int height, int updateMode): void {
        _native_rtt_create(cameraEntityId, width, height, updateMode);
        _native_rtt_resize(cameraEntityId, width, height);
        _native_rtt_setUpdateMode(cameraEntityId, updateMode);
        _native_rtt_setEnabled(cameraEntityId, true);
    }
}
