import * from "../core/collections/HashMap.mt";
import * from "../core/primitives/String.mt";

public value class HttpResponse {
    public int status;
    public string body;
    public HashMap<String, String> headers;

    public constructor(int status, string body, HashMap<String, String> headers) {
        this.status = status;
        this.body = body;
        this.headers = headers;
    }

    public constructor() {
        this.status = 0;
        this.body = "";
        this.headers = new HashMap<String, String>();
    }

    public function isOk(): bool {
        return this.status >= 200 && this.status < 300;
    }

    public function header(string name): string {
        if (this.headers == null) {
            return "";
        }
        String v = this.headers.get(new String(name));
        if (v == null) {
            return "";
        }
        return v.getValue();
    }

    public function toString(): string {
        return "HttpResponse(status=" + this.status + ", bodyLen=" + this.body.length() + ")";
    }
}
