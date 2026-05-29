import * from "NetworkException.mt";

class DnsException extends NetworkException {
    public constructor(string msg) : super(msg) {
    }

    public function toString(): string {
        return "DnsException: " + message;
    }
}
