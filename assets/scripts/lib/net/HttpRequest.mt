import * from "../core/collections/HashMap.mt";
import * from "../core/primitives/String.mt";
import * from "HttpResponse.mt";

class HttpRequest {
    public string method;
    public string url;
    public string body;
    public HashMap<String, String> headers;
    public int timeoutMs;

    public constructor(string method, string url) {
        this.method = method;
        this.url = url;
        this.body = "";
        this.headers = new HashMap<String, String>();
        this.timeoutMs = 30000;
    }

    public function setHeader(string name, string value): HttpRequest {
        this.headers.put(new String(name), new String(value));
        return this;
    }

    public function setBody(string body): HttpRequest {
        this.body = body;
        return this;
    }

    public function setTimeout(int ms): HttpRequest {
        this.timeoutMs = ms;
        return this;
    }

    public function async sendAsync(): Promise<HttpResponse> {
        HttpResponse r = await __net_http_sendAsync(this.method, this.url, this.body, this.headers, this.timeoutMs);
        return r;
    }

    public function send(): HttpResponse {
        return await this.sendAsync();
    }
}
