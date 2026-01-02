// NullPointerException - thrown when accessing null object
import * from "RuntimeException.mt";

class NullPointerException extends RuntimeException {
    // Constructor with message
    public constructor(string msg) : super(msg) {
    }

    // Default constructor
    public constructor() : super("Attempted to access null object") {
    }

    // Convert to string
    public function toString(): string {
        return "NullPointerException: " + message;
    }
}
