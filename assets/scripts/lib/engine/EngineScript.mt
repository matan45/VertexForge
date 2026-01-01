// EngineScript - Base class for all game scripts
// All scripts must extend this class to integrate with the VertexForge engine
//
// Lifecycle methods:
//   onStart()  - Called once when the script is first enabled
//   onUpdate(deltaTime) - Called every frame while the script is enabled
//   onDestroy() - Called when the entity is destroyed or script is removed
//
// Native classes provided by engine (no import needed):
//   Log  - Logging utility (Log::info, Log::warn, Log::error)
//   Time - Time utilities (Time::getDeltaTime, Time::getTime)
//
// Entity access is handled internally by the engine.
// Use the protected helper methods below to access entity properties.

import * from "../math/Vec3f.mt";
import * from "../math/Quaternion.mt";

public abstract class EngineScript {
    // Internal entity ID - set by engine (do not modify)
    protected int _entityId = 0;

    // Default constructor - required for engine instantiation
    public constructor() {
    }

    // Lifecycle method: Called once when the script starts
    public function onStart(): void {
    }

    // Lifecycle method: Called every frame
    public function onUpdate(float deltaTime): void {
    }

    // Lifecycle method: Called when the entity is destroyed
    public function onDestroy(): void {
    }
}
