#pragma once
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>

namespace services { class ScriptInterpreter; }
namespace debugger { class DebugServer; }
namespace net { class ISocket; class ISocketServer; }

namespace core
{
    // VK-1371: embeds mType's debugger::DebugServer inside the engine and drives
    // it over a TCP socket so an external client (the mType VS Code extension,
    // once MYT-379 lands its "attach" mode) can connect and debug the scripts the
    // engine executes - breakpoints/stepping inside onStart/onUpdate/etc.
    //
    // No mType source changes are needed: the debugger and the net socket classes
    // already ship inside the linked mType.lib, and DebugServer::run(readLine) +
    // DebugProtocol::setProtocolWriter are transport-agnostic hooks we feed with a
    // socket here instead of the standalone mType.exe's stdin/stdout.
    //
    // Threading: a hit breakpoint blocks the script-executing thread (the main
    // thread for onUpdate) in DebugHookHelper::waitForResume(); the editor frame
    // therefore freezes until the client resumes/steps. The debug server itself
    // runs on a separate thread (the socket accept worker), so it stays responsive
    // and can resume even while the script thread is blocked - no deadlock. If the
    // client disconnects, the paused script is released so the engine never stays
    // frozen.
    class ScriptDebugServer
    {
    public:
        ScriptDebugServer();
        ~ScriptDebugServer();

        ScriptDebugServer(const ScriptDebugServer&) = delete;
        ScriptDebugServer& operator=(const ScriptDebugServer&) = delete;

        // Enable debugging on `interpreter`, start listening on `port`, and run the
        // debug server on a background thread. Idempotent: a no-op while active.
        void start(::services::ScriptInterpreter* interpreter, int port);

        // Stop the server, release any script thread paused at a breakpoint, and
        // disable debugging on the interpreter. Idempotent.
        void stop();

        bool isActive() const { return active.load(); }

    private:
        // Reads one newline-delimited protocol line from the client socket; returns
        // false when the socket is closed (ends the DebugServer run loop).
        bool readLine(std::string& outLine);

        ::services::ScriptInterpreter* interpreter = nullptr;
        std::unique_ptr<net::ISocketServer> listener;
        std::unique_ptr<debugger::DebugServer> server;

        std::shared_ptr<net::ISocket> clientSocket;
        std::mutex clientMutex;
        std::string recvBuffer;

        std::atomic<bool> active{false};
        std::atomic<bool> clientConnected{false};
        int listenPort = 0;
    };
}
