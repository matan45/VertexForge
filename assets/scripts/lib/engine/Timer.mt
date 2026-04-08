// Timer - Convenience timer utilities built on Coroutine primitives
// Provides simple delay functions for common timing patterns.
//
// Usage examples:
//   await Timer.delay(2.0);           // Wait 2 seconds
//   await Timer.delayFrames(10);      // Wait 10 frames
//
// For repeating timers, use an async loop:
//   public async function heartbeat(): Promise<void> {
//       while (alive) {
//           await Timer.delay(1.0);
//           pulse();
//       }
//   }

import * from "Coroutine.mt";

public class Timer {

    public constructor() {
    }

    // One-shot delay for the specified number of seconds
    public static function async delay(float seconds): Promise<void> {
        await Coroutine::waitForSeconds(seconds);
    }

    // One-shot delay for the specified number of frames
    public static function async delayFrames(int count): Promise<void> {
        await Coroutine::waitForFrames(count);
    }
}
