// JsonEventCallback - Functional interface for event listeners that receive a payload
//
// Used with ScriptEvent::listenJson. The engine hands the callback ONE string: the event
// payload serialized as JSON. Engine plugins publish such payloads on the plugin event bus
// (PluginContext::publishEvent); ScriptEvent::listenJson is how a script receives them.
//
// Parse the payload with Json from "lib/core/json/Json.mt":
//   ScriptEvent::listenJson("rts.unit_killed", (string json) -> {
//       KillPayload p = Json::deserializeAs(json, "KillPayload");
//       Log::info("victim " + parsePrimitive(p.victimId));
//   });
//
// A plain EventCallback registered with ScriptEvent::listen still fires for the same event —
// its invoke() takes no arguments, so it just does not see the payload.

public interface JsonEventCallback {
    function invoke(string json): void;
}
