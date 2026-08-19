// App - Application lifetime control from scripts
//
// quit(exitCode) behaves differently per host, on purpose:
//   Runtime.exe : closes the window and the process returns exitCode from main(), so an
//                 automated match can report pass (0) / fail (non-zero) to a test harness.
//   Editor      : stops Play mode. The editor itself never exits; exitCode is only logged.
//
// The request is applied on the main thread once the current frame finishes, so calling it
// from onUpdate/onLateUpdate is safe — the rest of this frame's scripts still run.
//
// Usage:
//   App::quit(0);   // success
//   App::quit(1);   // failure

public class App {
    public constructor() {
    }

    public static function quit(int exitCode): void {
        _native_app_quit(exitCode);
    }
}
