// ScriptEvent - Decoupled pub/sub event system for cross-script communication
//
// Usage examples:
//   int token = ScriptEvent::listen("playerDied", () -> {
//       Log::info("Player died!");
//   });
//   ScriptEvent::emit("playerDied");
//   ScriptEvent::unlisten(token);

import * from "EventCallback.mt";

public class ScriptEvent {
    public constructor() {
    }

    // Subscribe: when eventName fires, invoke the callback
    // Returns a token for unsubscribing
    public static function listen(string eventName, EventCallback callback): int {
        return _native_scriptEvent_listen(eventName, callback);
    }

    // Unsubscribe using the token returned by listen
    public static function unlisten(int token): void {
        _native_scriptEvent_unlisten(token);
    }

    // Fire an event to all listeners
    public static function emit(string eventName): void {
        _native_scriptEvent_emit(eventName);
    }
}
