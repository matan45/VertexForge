// ScriptCallback - Functional interface for cross-script communication
//
// Used with Entity::sendMessage and Entity::broadcastMessage
// The callback receives each script object attached to the entity
//
// Usage:
//   Entity::sendMessage(entityId, (script) -> {
//       // do something with script
//   });

public interface ScriptCallback {
    function invoke(Object script): void;
}
