import * from "NetworkException.mt";

class HttpException extends NetworkException {
    public int status;

    public constructor(string msg) : super(msg) {
        status = 0;
    }

    public function toString(): string {
        return "HttpException(" + status + "): " + message;
    }
}
