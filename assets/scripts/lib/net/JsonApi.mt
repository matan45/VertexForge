import * from "../core/collections/HashMap.mt";
import * from "../core/primitives/String.mt";
import * from "../core/json/Json.mt";
import * from "Http.mt";
import * from "HttpRequest.mt";
import * from "HttpResponse.mt";
import * from "exceptions/HttpException.mt";

// Convenience wrapper that combines the HTTP client with the JSON serializer.
// Each request applies the configured baseUrl + default headers, sends via the
// async HTTP native, and parses the response body with the existing Json class.
class JsonApi {
    public string baseUrl;
    public HashMap<String, String> defaultHeaders;
    public int timeoutMs;

    public constructor(string baseUrl) {
        this.baseUrl = baseUrl;
        this.defaultHeaders = new HashMap<String, String>();
        this.defaultHeaders.put(new String("Accept"), new String("application/json"));
        this.timeoutMs = 30000;
    }

    public function setHeader(string name, string value): JsonApi {
        this.defaultHeaders.put(new String(name), new String(value));
        return this;
    }

    public function setTimeout(int ms): JsonApi {
        this.timeoutMs = ms;
        return this;
    }

    private function buildUrl(string path): string {
        return this.baseUrl + path;
    }

    private function async sendBody(string method, string path, string body): Promise<HttpResponse> {
        HashMap<String, String> headers = this.defaultHeaders;
        HttpResponse r = await __net_http_sendAsync(method, this.buildUrl(path), body, headers, this.timeoutMs);
        return r;
    }

    public function async get(string path): Promise<String> {
        HttpResponse r = await this.sendBody("GET", path, "");
        if (!r.isOk()) {
            HttpException ex = new HttpException("GET " + path + " returned " + r.status);
            ex.status = r.status;
            throw ex;
        }
        return new String(r.body);
    }

    public function async post(string path, string jsonBody): Promise<String> {
        HttpResponse r = await this.sendBody("POST", path, jsonBody);
        if (!r.isOk()) {
            HttpException ex = new HttpException("POST " + path + " returned " + r.status);
            ex.status = r.status;
            throw ex;
        }
        return new String(r.body);
    }

    public function async put(string path, string jsonBody): Promise<String> {
        HttpResponse r = await this.sendBody("PUT", path, jsonBody);
        if (!r.isOk()) {
            HttpException ex = new HttpException("PUT " + path + " returned " + r.status);
            ex.status = r.status;
            throw ex;
        }
        return new String(r.body);
    }

    public function async delete(string path): Promise<String> {
        HttpResponse r = await this.sendBody("DELETE", path, "");
        if (!r.isOk()) {
            HttpException ex = new HttpException("DELETE " + path + " returned " + r.status);
            ex.status = r.status;
            throw ex;
        }
        return new String(r.body);
    }

    // Generic typed accessor: GET path and deserialize as className.
    public function async <T> getAs(string path, string className): Promise<T> {
        String body = await this.get(path);
        T result = Json::deserializeAs(body.getValue(), className);
        return result;
    }

    public function async <T> postAs(string path, T obj, string className): Promise<T> {
        string jsonBody = Json::serialize(obj);
        String body = await this.post(path, jsonBody);
        T result = Json::deserializeAs(body.getValue(), className);
        return result;
    }
}
