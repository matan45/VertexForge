// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <vm/runtime/VirtualMachine.hpp>
#include <debugger/DebugContext.hpp>
#include <debugger/DebugProtocol.hpp>
#include <net/WinSocket.hpp>

#include "ScriptDebugServer.hpp"
#include "print/Log.hpp"

namespace core
{
    ScriptDebugServer::ScriptDebugServer() = default;

    ScriptDebugServer::~ScriptDebugServer()
    {
        stop();
    }

    void ScriptDebugServer::start(::services::ScriptInterpreter* interp, int port)
    {
        if (active.load() || interp == nullptr)
        {
            return;
        }

        interpreter = interp;
        listenPort = port;
        clientConnected = false;
        {
            std::lock_guard<std::mutex> lock(clientMutex);
            recvBuffer.clear();
        }

        // Enable the debugger singleton, then immediately switch to CONTINUE so the
        // engine keeps running and only pauses once a real breakpoint arrives over
        // the wire (enable() leaves it in PAUSED mode, which we don't want for an
        // attach-mid-flight host).
        debugger::DebugContext::initialize();
        debugger::DebugContext::getInstance().continueExecution();
        interpreter->enableDebugging();

        // VK-1378: the breakpoint hook lives on the interpreter execution paths;
        // JIT-compiled code bypasses it. Debugging and the JIT are mutually
        // exclusive, so force the script VM into interpreter mode while the
        // debug server is attached. Remember the prior JIT state so stop() can
        // restore it — Play mode runs with JIT on, only the debug session is off.
        if (auto vm = interpreter->getVM())
        {
            jitWasEnabled = vm->isJitEnabled();
            vm->setJitEnabled(false);
        }

        server = std::make_unique<debugger::DebugServer>();
        server->setEnvironment(interpreter->getEnvironment());
        server->setVM(interpreter->getVM());

        // Route every protocol message (responses + STOPPED/OUTPUT events) to the
        // connected client instead of stdout.
        debugger::DebugProtocol::setProtocolWriter(
            [this](const std::string& line)
            {
                std::shared_ptr<net::ISocket> sock;
                {
                    std::lock_guard<std::mutex> lock(clientMutex);
                    sock = clientSocket;
                }
                if (sock)
                {
                    // Never let a socket error escape: this runs on the debug
                    // worker thread, and an uncaught throw would terminate it.
                    try { sock->send(line + "\n"); }
                    catch (...) { /* client gone; the read side detects EOF */ }
                }
            });

        listener = std::make_unique<net::WinSocketServer>();
        active = true;

        listener->start(
            listenPort,
            [this](uintptr_t fd)
            {
                // v1 accepts a single debugger client; reject extras.
                bool expected = false;
                if (!clientConnected.compare_exchange_strong(expected, true))
                {
                    net::WinSocket reject(fd);
                    reject.close();
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(clientMutex);
                    clientSocket = std::make_shared<net::WinSocket>(fd);
                    recvBuffer.clear();
                }

                // Fresh attach: run freely, pause only at breakpoints the client sets.
                debugger::DebugContext::getInstance().continueExecution();

                // Blocks this accept-worker thread until the client disconnects.
                server->run([this](std::string& line) { return readLine(line); });

                // Client gone: drop the socket, clear its breakpoints, and release a
                // script thread that may be parked in waitForResume() so the engine
                // never stays frozen after a detach.
                {
                    std::lock_guard<std::mutex> lock(clientMutex);
                    clientSocket.reset();
                }
                debugger::DebugContext::getInstance().clearAllBreakpoints();
                debugger::DebugContext::getInstance().continueExecution();
                clientConnected = false;
            },
            [](const std::string& err)
            {
                vfLogWarning("mType debug server accept error: {}", err);
            });

        vfLogInfo("mType debug server listening on port {} (VS Code: attach to localhost:{})",
                  listenPort, listenPort);
    }

    void ScriptDebugServer::stop()
    {
        if (!active.exchange(false))
        {
            return;
        }

        // Release any script thread paused at a breakpoint (STOPPED makes
        // waitForResume() return) and tell the server loop to exit.
        debugger::DebugContext::getInstance().stop();
        if (server)
        {
            server->stop();
        }

        // Close the client socket so the blocking recv() in readLine() returns and
        // the accept-worker thread can finish running server->run().
        {
            std::lock_guard<std::mutex> lock(clientMutex);
            if (clientSocket)
            {
                clientSocket->close();
            }
        }

        // Stops the accept loop and joins the worker thread (safe now that run()
        // can return).
        if (listener)
        {
            listener->stop();
            listener.reset();
        }

        {
            std::lock_guard<std::mutex> lock(clientMutex);
            clientSocket.reset();
        }
        clientConnected = false;

        debugger::DebugProtocol::setProtocolWriter(nullptr);
        server.reset();
        if (interpreter)
        {
            interpreter->disableDebugging();
            // Restore the JIT to whatever it was before we attached, so leaving the
            // debugger doesn't leave Play mode stuck in interpreter mode.
            if (auto vm = interpreter->getVM())
            {
                vm->setJitEnabled(jitWasEnabled);
            }
        }
        debugger::DebugContext::shutdown();
        interpreter = nullptr;

        vfLogInfo("mType debug server stopped");
    }

    bool ScriptDebugServer::readLine(std::string& outLine)
    {
        for (;;)
        {
            // recvBuffer is shared with start()/onAccept (which clear it under
            // clientMutex), so every read/mutate of it is taken under the lock too.
            // The lock is released around the blocking recv() below so stop() can
            // still grab clientMutex to close the socket and unblock us.
            std::shared_ptr<net::ISocket> sock;
            {
                std::lock_guard<std::mutex> lock(clientMutex);
                std::size_t nl = recvBuffer.find('\n');
                if (nl != std::string::npos)
                {
                    outLine = recvBuffer.substr(0, nl);
                    if (!outLine.empty() && outLine.back() == '\r')
                    {
                        outLine.pop_back();
                    }
                    recvBuffer.erase(0, nl + 1);
                    return true;
                }
                sock = clientSocket;
            }
            if (!sock)
            {
                return false;
            }

            // recv() throws on socket error (e.g. when stop() closes the socket
            // from the main thread to unblock us). This runs on the worker
            // thread, so an escaping exception would terminate the process —
            // treat any failure as EOF and end the loop cleanly.
            std::string chunk;
            try
            {
                chunk = sock->recv(4096);
            }
            catch (...)
            {
                return false;
            }
            if (chunk.empty())
            {
                return false; // socket closed
            }
            {
                std::lock_guard<std::mutex> lock(clientMutex);
                recvBuffer += chunk;
            }
        }
    }
}
