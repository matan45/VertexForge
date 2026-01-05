// Log - Static utility class for logging from scripts
// Wraps native engine logging functions

public class Log {
    public constructor() {
    }

    public static function info(string message): void {
        _native_log_info(message);
    }

    public static function warn(string message): void {
        _native_log_warn(message);
    }

    public static function error(string message): void {
        _native_log_error(message);
    }
}
