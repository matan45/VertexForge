// ClassNotFoundException - thrown when a reflection lookup fails to resolve a class
import * from "RuntimeException.mt";

class ClassNotFoundException extends RuntimeException {
    // Constructor with message
    public constructor(string msg) : super(msg) {
    }

    // Default constructor
    public constructor() : super("Class not found") {
    }

    // Convert to string
    public function toString(): string {
        return "ClassNotFoundException: " + message;
    }
}
