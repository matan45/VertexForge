// RenderTexture - Static utility class for render-to-texture operations
// Works with entity IDs (int) to control RTT cameras
//
// Usage examples:
//   int rttEntity = Entity::findByName("SecurityCamera");
//   RenderTexture::create(rttEntity, 256, 256, RenderTextureUpdateMode::CONTINUOUS);
//   RenderTexture::setCamera(rttEntity, cameraEntity);
//   RenderTexture::setPriority(rttEntity, 1);
//   RenderTexture::requestRender(rttEntity);
//   RenderTexture::destroy(rttEntity);

import * from "RenderTextureUpdateMode.mt";

public class RenderTexture {
    public constructor() {
    }

    // ============================================
    // Lifecycle
    // ============================================

    // Create a render texture for an entity that has a RenderTextureComponent.
    // updateMode: use RenderTextureUpdateMode constants (EVERY_FRAME, ON_DEMAND, FIXED_INTERVAL)
    public static function create(int entityId, int width, int height, int updateMode): void {
        _native_rtt_create(entityId, width, height, updateMode);
    }

    // Destroy the render texture associated with an entity.
    public static function destroy(int entityId): void {
        _native_rtt_destroy(entityId);
    }

    // ============================================
    // Configuration
    // ============================================

    // Resize the render texture.
    public static function resize(int entityId, int width, int height): void {
        _native_rtt_resize(entityId, width, height);
    }

    // Assign a camera to this RTT. Reads view/projection from the camera entity.
    public static function setCamera(int rttEntityId, int cameraEntityId): void {
        _native_rtt_setCamera(rttEntityId, cameraEntityId);
    }

    // Set the rendering priority (higher renders first).
    public static function setPriority(int entityId, int priority): void {
        _native_rtt_setPriority(entityId, priority);
    }

    // Set the update mode. Use RenderTextureUpdateMode constants.
    public static function setUpdateMode(int entityId, int mode): void {
        _native_rtt_setUpdateMode(entityId, mode);
    }

    // ============================================
    // State
    // ============================================

    // Request a single render for an OnDemand RTT camera.
    // The flag is consumed after the next frame — call again for another render.
    public static function requestRender(int entityId): void {
        _native_rtt_requestRender(entityId);
    }

    // Enable or disable the RTT camera on the entity.
    public static function setEnabled(int entityId, bool enabled): void {
        _native_rtt_setEnabled(entityId, enabled);
    }

    // Check whether the RTT camera is currently enabled.
    public static function isEnabled(int entityId): bool {
        return _native_rtt_isEnabled(entityId);
    }

    // ============================================
    // Queries
    // ============================================

    // Get the current width of the render texture.
    public static function getWidth(int entityId): int {
        return _native_rtt_getWidth(entityId);
    }

    // Get the current height of the render texture.
    public static function getHeight(int entityId): int {
        return _native_rtt_getHeight(entityId);
    }
}
