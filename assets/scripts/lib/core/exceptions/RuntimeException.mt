// RuntimeException - base class for runtime errors
import * from "Exception.mt";

class RuntimeException extends Exception {
    // Constructor with message
    public constructor(string msg) : super(msg) {
    }

    // Default constructor
    public constructor() : super("Runtime exception occurred") {
    }

    // Convert to string
    public function toString(): string {
        return "RuntimeException: " + message;
    }
}
