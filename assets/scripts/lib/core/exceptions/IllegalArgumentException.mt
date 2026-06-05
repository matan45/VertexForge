// IllegalArgumentException - thrown when invalid argument is passed
import * from "RuntimeException.mt";

class IllegalArgumentException extends RuntimeException {
    public string argumentName;

    // Constructor with message
    public constructor(string msg) : super(msg) {
        argumentName = "";
    }

    // Default constructor
    public constructor() : super("Illegal argument") {
        argumentName = "";
    }

    // Get the argument name
    public function getArgumentName(): string {
        return argumentName;
    }

    // Convert to string
    public function toString(): string {
        return "IllegalArgumentException: " + message;
    }
}
