import * from "NetworkException.mt";

class ConnectionException extends NetworkException {
    public constructor(string msg) : super(msg) {
    }

    public function toString(): string {
        return "ConnectionException: " + message;
    }
}
