// Config - Static utility class for persistent key-value settings
// Stores player preferences in a JSON file that persists across sessions
//
// Usage examples:
//   Config::setFloat("musicVolume", 0.8);
//   var vol = Config::getFloat("musicVolume", 1.0);  // returns 0.8
//
//   Config::setString("playerName", "Hero");
//   var name = Config::getString("playerName", "Player");
//
//   Config::setBool("fullscreen", true);
//   Config::setInt("difficulty", 2);

public class Config {
    public constructor() {
    }

    // === Int ===

    public static function setInt(string key, int value): void {
        _native_config_setInt(key, value);
    }

    public static function getInt(string key, int defaultValue): int {
        return _native_config_getInt(key, defaultValue);
    }

    // === Float ===

    public static function setFloat(string key, float value): void {
        _native_config_setFloat(key, value);
    }

    public static function getFloat(string key, float defaultValue): float {
        return _native_config_getFloat(key, defaultValue);
    }

    // === String ===

    public static function setString(string key, string value): void {
        _native_config_setString(key, value);
    }

    public static function getString(string key, string defaultValue): string {
        return _native_config_getString(key, defaultValue);
    }

    // === Bool ===

    public static function setBool(string key, bool value): void {
        _native_config_setBool(key, value);
    }

    public static function getBool(string key, bool defaultValue): bool {
        return _native_config_getBool(key, defaultValue);
    }
}
