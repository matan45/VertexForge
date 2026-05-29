import * from "../core/primitives/String.mt";
import * from "../core/primitives/Int.mt";

// TCP client socket. Holds an integer handle into the C++ SocketRegistry; all
// methods delegate to __net_socket_* natives.
//
// Two flavors of every I/O method are exposed:
//   * connectAsync/sendAsync/receiveAsync — return Promise<T>; use directly
//     from async contexts with `await`.
//   * connect/send/receive — sync wrappers that internally `await` the async
//     variant. Safe to call from any async function (top-level async script,
//     async test method, or an async-lambda callback). Calling them from a
//     non-async event-loop callback would deadlock — use the async variants
//     in that case (and make the callback an `async` lambda).
class TcpSocket {
    public int handle;

    public constructor() {
        this.handle = __net_socket_create();
    }

    public function async connectAsync(string host, int port): Promise<void> {
        await __net_socket_connectAsync(this.handle, host, port);
    }

    public function connect(string host, int port): void {
        await this.connectAsync(host, port);
    }

    public function async sendAsync(string data): Promise<Int> {
        int sent = await __net_socket_sendAsync(this.handle, data);
        return new Int(sent);
    }

    public function send(string data): int {
        Int sent = await this.sendAsync(data);
        return sent.getValue();
    }

    public function async receiveAsync(int maxBytes): Promise<String> {
        string s = await __net_socket_receiveAsync(this.handle, maxBytes);
        return new String(s);
    }

    public function receive(int maxBytes): string {
        String s = await this.receiveAsync(maxBytes);
        return s.getValue();
    }

    public function close(): void {
        __net_socket_close(this.handle);
    }

    public function isConnected(): bool {
        return __net_socket_isConnected(this.handle);
    }

    public function setTimeout(int ms): void {
        __net_socket_setTimeout(this.handle, ms);
    }
}
