import * from "../core/collections/HashMap.mt";
import * from "../core/primitives/String.mt";
import * from "HttpRequest.mt";
import * from "HttpResponse.mt";

// Static facade for HTTP requests. Sync methods delegate to the async natives
// and await internally; pass an HttpRequest to override timeouts or headers.
class Http {
    public constructor() { }

    // ---- Async ------------------------------------------------------------

    public static function async getAsync(string url): Promise<HttpResponse> {
        HashMap<String, String> headers = new HashMap<String, String>();
        HttpResponse r = await __net_http_sendAsync("GET", url, "", headers, 30000);
        return r;
    }

    public static function async postAsync(string url, string body): Promise<HttpResponse> {
        HashMap<String, String> headers = new HashMap<String, String>();
        headers.put(new String("Content-Type"), new String("application/json"));
        HttpResponse r = await __net_http_sendAsync("POST", url, body, headers, 30000);
        return r;
    }

    public static function async putAsync(string url, string body): Promise<HttpResponse> {
        HashMap<String, String> headers = new HashMap<String, String>();
        headers.put(new String("Content-Type"), new String("application/json"));
        HttpResponse r = await __net_http_sendAsync("PUT", url, body, headers, 30000);
        return r;
    }

    public static function async deleteAsync(string url): Promise<HttpResponse> {
        HashMap<String, String> headers = new HashMap<String, String>();
        HttpResponse r = await __net_http_sendAsync("DELETE", url, "", headers, 30000);
        return r;
    }

    // ---- Sync (wrappers around the async path) ----------------------------

    public static function get(string url): HttpResponse {
        return await Http::getAsync(url);
    }

    public static function post(string url, string body): HttpResponse {
        return await Http::postAsync(url, body);
    }

    public static function put(string url, string body): HttpResponse {
        return await Http::putAsync(url, body);
    }

    public static function delete(string url): HttpResponse {
        return await Http::deleteAsync(url);
    }

    // ---- Builder entry point ---------------------------------------------

    public static function request(string method, string url): HttpRequest {
        return new HttpRequest(method, url);
    }
}
