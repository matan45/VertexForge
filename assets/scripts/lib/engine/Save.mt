// Save - Static utility class for game save/load operations
// Manages save slots with scene state and script field persistence
//
// Usage examples:
//   Save::createSlot("slot1");
//   Save::save("slot1");                    // Saves scene + all @Saveable script fields
//   Save::load("slot1");                    // Restores scene + script fields
//   var slots = Save::listSlots();          // ["slot1", "autosave"]
//   var meta = Save::getMetadata("slot1");  // JSON string with timestamp, playtime
//   Save::delete("slot1");
//
// Mark script classes with @Saveable to include their fields in save data:
//   @Saveable
//   @Script
//   public class PlayerState {
//       var health: int = 100;
//       var level: int = 1;
//   }

public class Save {
    public constructor() {
    }

    // Create a named save slot (directory on disk)
    // Call this before saving if you want to ensure the slot exists
    public static function createSlot(string name): void {
        _native_save_createSlot(name);
    }

    // Save current game state to a named slot
    // Serializes the current scene and all @Saveable script instance fields
    public static function save(string slotName): void {
        _native_save_save(slotName);
    }

    // Load game state from a named save slot
    // Replaces the current scene and restores @Saveable script fields
    public static function load(string slotName): void {
        _native_save_load(slotName);
    }

    // List all available save slot names
    public static function listSlots(): string[] {
        return _native_save_listSlots();
    }

    // Delete a save slot and all its data
    public static function delete(string slotName): void {
        _native_save_delete(slotName);
    }

    // Get metadata for a save slot as a JSON string
    // Contains: slotName, timestamp, playtimeSeconds, customData
    public static function getMetadata(string slotName): string {
        return _native_save_getMetadata(slotName);
    }
}
