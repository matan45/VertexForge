// EventCallback - Functional interface for script event listeners
//
// Used with ScriptEvent::listen to subscribe to custom events
//
// Usage:
//   ScriptEvent::listen("playerDied", () -> {
//       Log::info("Player died!");
//   });

public interface EventCallback {
    function invoke(): void;
}
