// SimpleTest - Minimal script to test scripting system
// Uses @Script annotation for engine integration

import * from "../lib/engine/Log.mt";

@Script
public class SimpleTest {
    private int frameCount = 0;

    public constructor() {
        Log::info("SimpleTest: Constructor called");
    }

    public function onStart(): void {
        Log::info("=== SimpleTest: onStart() ===");
    }

    public function onUpdate(float deltaTime): void {
        frameCount = frameCount + 1;

        if (frameCount % 60 == 0) {
            Log::info("SimpleTest: Frame " + parsePrimitive(frameCount));
        }
    }

    public function onDestroy(): void {
        Log::info("=== SimpleTest: onDestroy() ===");
        Log::info("Total frames: " + parsePrimitive(frameCount));
    }
}
