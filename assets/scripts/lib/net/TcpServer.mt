import * from "AsyncConsumer.mt";
import * from "TcpSocket.mt";
import * from "exceptions/NetworkException.mt";

// TCP listening server. listen() spawns a worker accept thread in C++; for
// each accepted client the worker posts back to the VM event loop and the
// stored onConnection consumer is scheduled as a new event-loop task. The
// callback is async, so its body can `await` socket I/O. Always call stop().
class TcpServer {
    public int handle;

    public constructor() {
        this.handle = __net_tcpserver_create();
    }

    public function listen(int port): void {
        __net_tcpserver_listen(this.handle, port);
    }

    public function onConnection(AsyncConsumer<TcpSocket> callback): void {
        __net_tcpserver_onConnection(this.handle, callback);
    }

    public function onError(AsyncConsumer<NetworkException> callback): void {
        __net_tcpserver_onError(this.handle, callback);
    }

    public function stop(): void {
        __net_tcpserver_stop(this.handle);
    }
}
