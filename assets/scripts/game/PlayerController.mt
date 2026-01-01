// PlayerController - Example game script
// Demonstrates basic movement using EngineScript lifecycle methods

import * from "../lib/engine/EngineScript.mt";
import * from "../lib/math/Vec3f.mt";

public class PlayerController extends EngineScript {
    // Movement speed in units per second
    private float speed = 5.0;

    // Track total time for demonstration
    private float totalTime = 0.0;

    public constructor() : super() {
        // Initialize with default values
    }

    public function onStart(): void {
        Log::info("PlayerController started on entity: " + getName());
        Vec3f pos = getPosition();
        Log::info("Initial position: " + pos.toString());
    }

    public function onUpdate(float deltaTime): void {
        totalTime = totalTime + deltaTime;

        // Example: Move the entity upward over time
        Vec3f currentPos = getPosition();
        Vec3f movement = new Vec3f(0.0, speed * deltaTime, 0.0);
        Vec3f newPos = currentPos.add(movement);
        setPosition(newPos);
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
