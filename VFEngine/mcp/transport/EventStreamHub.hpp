#pragma once

#include "HttpMessage.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace mcp::http
{
    // Fan-out for the standalone GET /mcp SSE stream (VK-1652): server -> client
    // JSON-RPC notifications such as notifications/tools/list_changed.
    //
    // Each open stream owns a queue; broadcast() enqueues one SSE event per stream
    // and serve() (on the stream's connection thread) writes them out. The mutex is
    // never held during a socket write. There is no replay: Last-Event-ID is
    // accepted by the caller and ignored.
    //
    // Threading: every member is thread-safe.
    class EventStreamHub
    {
    public:
        // Opening one more evicts the oldest (a 409 makes the TS SDK give up).
        static constexpr std::size_t maxStreams = 2;
        // Per-stream backlog; the oldest events are dropped beyond it.
        static constexpr std::size_t maxPendingEvents = 256;
        static constexpr std::chrono::milliseconds defaultKeepalive{15000};

        // All fields are guarded by the hub mutex.
        struct Stream
        {
            std::deque<std::string> pending;
            bool closed = false;  // evicted or hub closed
        };

        // Registers a new stream. nullptr when the hub is closed.
        std::shared_ptr<Stream> open();

        // Sends a single JSON-RPC message (compact JSON) to every open stream.
        // Dropped when no stream is open or the hub is closed.
        void broadcast(const std::string& jsonText);

        // Writes the stream's events to `sink` until the stream is evicted, the
        // hub closes, a write fails, the peer disconnects or the server stops.
        // Writes ": keepalive" after `keepalive` without events and probes the
        // peer then. Removes the stream from the hub on exit.
        void serve(const std::shared_ptr<Stream>& stream, StreamSink& sink,
                   std::chrono::milliseconds keepalive = defaultKeepalive);

        // Closes every stream and refuses new ones (McpService::stop()).
        void close();

        // Accepts streams again after close() (McpService::start()).
        void reset();

        bool isOpen() const;
        std::size_t streamCount() const;

        // "event: message\nid: <id>\ndata: <line>\n...\n" (one data: line per
        // line of `jsonText`).
        static std::string formatEvent(uint64_t id, std::string_view jsonText);

    private:
        void removeLocked(const std::shared_ptr<Stream>& stream);

        mutable std::mutex mutex;
        std::condition_variable wake;
        std::vector<std::shared_ptr<Stream>> streams;  // oldest first
        uint64_t nextEventId = 1;
        bool closed = false;
    };
}
