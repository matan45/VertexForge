// Functional interface for async callbacks. Used by TcpServer.onConnection
// and TcpServer.onError so handler bodies can `await` other async operations.
//
// Usage:
//   AsyncConsumer<TcpSocket> handler = async conn -> {
//       String data = await conn.receiveAsync(1024);
//       await conn.sendAsync("ECHO:" + data.getValue());
//       conn.close();
//   };
interface AsyncConsumer<T> {
    function async accept(T t): Promise<void>;
}
