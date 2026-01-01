// TestScript - Simple script to verify scripting system works
// Logs messages on each lifecycle event

import * from "../lib/engine/EngineScript.mt";
import * from "../lib/engine/Log.mt";

public class TestScript extends EngineScript {
    private int frameCount = 0;
    private float elapsedTime = 0.0;

    public constructor() : super() {
    }

    public function onStart(): void {
        Log::info("=== TestScript: onStart() called ===");
    }

    public function onUpdate(float deltaTime): void {
        frameCount = frameCount + 1;
        elapsedTime = elapsedTime + deltaTime;

        // Log every 60 frames (roughly every second at 60fps)
        if (frameCount % 60 == 0) {
            Log::info("TestScript: Frame " + parsePrimitive(frameCount) + " | Time: " + parsePrimitive(elapsedTime) + "s");
        }
    }

    public function onDestroy(): void {
        Log::info("=== TestScript: onDestroy() called ===");
        Log::info("Total frames: " + parsePrimitive(frameCount));
    }
}
