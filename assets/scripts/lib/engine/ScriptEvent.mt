// ScriptEvent - Decoupled pub/sub event system for cross-script communication
//
// Usage examples:
//   int token = ScriptEvent::listen("playerDied", () -> {
//       Log::info("Player died!");
//   });
//   ScriptEvent::emit("playerDied");
//   ScriptEvent::unlisten(token);
//
// Engine plugins publish on the same event names through PluginContext::publishEvent, and
// those events carry a JSON object. Use listenJson to receive it — the payload arrives as
// ONE string argument (the JSON object serialized); parse it with Json from
// "lib/core/json/Json.mt". Plain listen() callbacks still fire for plugin events, they just
// do not see the payload.

import * from "EventCallback.mt";
import * from "JsonEventCallback.mt";

public class ScriptEvent {
    public constructor() {
    }

    // Subscribe: when eventName fires, invoke the callback
    // Returns a token for unsubscribing
    public static function listen(string eventName, EventCallback callback): int {
        return _native_scriptEvent_listen(eventName, callback);
    }

    // Subscribe with a payload: the callback receives the event's JSON payload as a string.
    // Returns a token for unsubscribing (same token space as listen).
    public static function listenJson(string eventName, JsonEventCallback callback): int {
        return _native_scriptEvent_listenJson(eventName, callback);
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
