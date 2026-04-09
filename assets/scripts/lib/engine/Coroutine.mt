// Coroutine - Frame-based coroutine primitives using async/await
// Provides functions to suspend execution for a number of frames, seconds,
// or until the next fixed update step.
//
// Usage examples:
//   await Coroutine.waitForSeconds(2.0);     // Wait 2 seconds
//   await Coroutine.waitForFrames(60);       // Wait 60 frames
//   await Coroutine.waitForNextFrame();      // Wait 1 frame
//   await Coroutine.waitForFixedUpdate();    // Wait for next physics step
//
// Coroutines must be used inside async functions:
//   public async function patrol(): Promise<void> {
//       while (alive) {
//           moveToNextWaypoint();
//           await Coroutine.waitForSeconds(1.0);
//       }
//   }

public class Coroutine {

    public constructor() {
    }

    // Wait for the specified number of seconds before continuing
    public static function async waitForSeconds(float seconds): Promise<void> {
        await _native_coroutine_waitForSeconds(seconds);
    }

    // Wait for the specified number of frames before continuing
    public static function async waitForFrames(int frames): Promise<void> {
        await _native_coroutine_waitForFrames(frames);
    }

    // Wait until the next frame
    public static function async waitForNextFrame(): Promise<void> {
        await _native_coroutine_waitForNextFrame();
    }

    // Wait until the next physics fixed update step
    public static function async waitForFixedUpdate(): Promise<void> {
        await _native_coroutine_waitForFixedUpdate();
    }
}
