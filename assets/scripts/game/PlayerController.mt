// PlayerController - Example game script
// Uses @Script annotation for engine integration

import * from "../lib/engine/Log.mt";
import * from "../lib/math/Vec3f.mt";

@Script
public class PlayerController {
    // Movement speed in units per second
    private float speed = 5.0;

    // Track total time for demonstration
    private float totalTime = 0.0;

    public constructor() {
        // Initialize with default values
    }

    public function onStart(): void {
        Log::info("PlayerController started!");
    }

    public function onUpdate(float deltaTime): void {
        totalTime = totalTime + deltaTime;

        // Example: Log every second
        if (parsePrimitive(totalTime) % 1.0 < deltaTime) {
            Log::info("PlayerController running... Time: " + parsePrimitive(totalTime) + "s");
        }
    }

    public function onDestroy(): void {
        Log::info("PlayerController destroyed. Total runtime: " + parsePrimitive(totalTime) + " seconds");
    }

    // Custom method: Set movement speed
    public function setSpeed(float newSpeed): void {
        this.speed = newSpeed;
    }

    // Custom method: Get current movement speed
    public function getSpeed(): float {
        return this.speed;
    }
}
