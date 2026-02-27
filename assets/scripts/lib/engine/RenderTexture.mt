// RenderTexture - Static utility class for render-to-texture operations
// Works with entity IDs (int) to control RTT cameras
//
// Usage examples:
//   int cameraEntity = Entity::findByName("SecurityCamera");
//   RenderTexture::requestRender(cameraEntity);   // Trigger one-shot render (OnDemand mode)
//   RenderTexture::setEnabled(cameraEntity, false); // Disable RTT camera
//   bool on = RenderTexture::isEnabled(cameraEntity);

public class RenderTexture {
    public constructor() {
    }

    // Request a single render for an OnDemand RTT camera.
    // The flag is consumed after the next frame — call again for another render.
    // Entity must have a RenderTextureComponent.
    public static function requestRender(int entityId): void {
        _native_rtt_requestRender(entityId);
    }

    // Enable or disable the RTT camera on the entity.
    // Disabled cameras skip rendering entirely.
    public static function setEnabled(int entityId, bool enabled): void {
        _native_rtt_setEnabled(entityId, enabled);
    }

    // Check whether the RTT camera is currently enabled.
    public static function isEnabled(int entityId): bool {
        return _native_rtt_isEnabled(entityId);
    }
}
