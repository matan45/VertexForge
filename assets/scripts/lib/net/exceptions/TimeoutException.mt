import * from "NetworkException.mt";

class TimeoutException extends NetworkException {
    public constructor(string msg) : super(msg) {
    }

    public function toString(): string {
        return "TimeoutException: " + message;
    }
}
