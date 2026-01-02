// Base Exception class for all exceptions in mType
class Exception {
    public string message;
    public string stackTrace;
	
	public constructor() {
        message = "";
        stackTrace = "";
    }

    // Constructor with message
    public constructor(string msg) {
        message = msg;
        stackTrace = "";
    }

    // Default constructor
    public constructor() {
        message = "An exception occurred";
        stackTrace = "";
    }

    // Get the exception message
    public function getMessage(): string {
        return message;
    }

    // Get the stack trace
    public function getStackTrace(): string {
        return stackTrace;
    }

    // Set the stack trace (used internally by the runtime)
    public function setStackTrace(string trace): void {
        stackTrace = trace;
    }

    // Convert exception to string
    public function toString(): string {
        return "Exception: " + message;
    }
}
