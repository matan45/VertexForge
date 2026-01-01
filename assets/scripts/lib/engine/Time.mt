// Time - Static utility class for time access from scripts
// Wraps native engine time functions

public class Time {
    public constructor() {
    }

    public static function getDeltaTime(): float {
        return _native_time_getDeltaTime();
    }

    public static function getTime(): float {
        return _native_time_getTime();
    }
}
