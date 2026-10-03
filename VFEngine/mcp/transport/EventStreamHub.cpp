#include "EventStreamHub.hpp"

#include <algorithm>

namespace mcp::http
{
    std::shared_ptr<EventStreamHub::Stream> EventStreamHub::open()
    {
        auto stream = std::make_shared<Stream>();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (closed)
            {
                return nullptr;
            }
            while (streams.size() >= maxStreams)
            {
                // Its serve() loop wakes, sees `closed` and ends that response.
                streams.front()->closed = true;
                streams.erase(streams.begin());
            }
            streams.push_back(stream);
        }
        wake.notify_all();
        return stream;
    }

    void EventStreamHub::broadcast(const std::string& jsonText)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (closed || streams.empty())
            {
                return;
            }
            std::string event = formatEvent(nextEventId++, jsonText);
            for (const auto& stream : streams)
            {
                if (stream->pending.size() >= maxPendingEvents)
                {
                    stream->pending.pop_front();
                }
                stream->pending.push_back(event);
            }
        }
        wake.notify_all();
    }

    void EventStreamHub::serve(const std::shared_ptr<Stream>& stream, StreamSink& sink,
                               std::chrono::milliseconds keepalive)
    {
        if (!stream)
        {
            return;
        }

        std::unique_lock<std::mutex> lock(mutex);
        for (;;)
        {
            if (closed || stream->closed || !sink.running())
            {
                break;
            }

            if (stream->pending.empty())
            {
                bool signalled = wake.wait_for(lock, keepalive,
                    [&]() { return closed || stream->closed || !stream->pending.empty(); });
                if (signalled)
                {
                    continue;
                }

                // Idle: the comment keeps intermediaries from timing the stream
                // out, and the probe notices a client that went away silently.
                lock.unlock();
                bool alive = sink.running() && sink.write(": keepalive\n\n") && !sink.peerClosed();
                lock.lock();
                if (!alive)
                {
                    break;
                }
                continue;
            }

            // Pop under the lock, write outside it.
            std::string batch;
            for (const std::string& event : stream->pending)
            {
                batch += event;
            }
            stream->pending.clear();

            lock.unlock();
            bool written = sink.write(batch);
            lock.lock();
            if (!written)
            {
                break;
            }
        }

        stream->closed = true;
        removeLocked(stream);
    }

    void EventStreamHub::close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            closed = true;
            for (const auto& stream : streams)
            {
                stream->closed = true;
            }
            streams.clear();
        }
        wake.notify_all();
    }

    void EventStreamHub::reset()
    {
        std::lock_guard<std::mutex> lock(mutex);
        closed = false;
    }

    bool EventStreamHub::isOpen() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return !closed;
    }

    std::size_t EventStreamHub::streamCount() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return streams.size();
    }

    std::string EventStreamHub::formatEvent(uint64_t id, std::string_view jsonText)
    {
        std::string out = "event: message\nid: " + std::to_string(id) + "\n";
        std::size_t pos = 0;
        for (;;)
        {
            std::size_t newline = jsonText.find('\n', pos);
            std::string_view line = jsonText.substr(pos, newline == std::string_view::npos ? std::string_view::npos
                                                                                           : newline - pos);
            if (!line.empty() && line.back() == '\r')
            {
                line.remove_suffix(1);
            }
            out += "data: ";
            out.append(line.data(), line.size());
            out += '\n';
            if (newline == std::string_view::npos)
            {
                break;
            }
            pos = newline + 1;
        }
        out += '\n';
        return out;
    }

    void EventStreamHub::removeLocked(const std::shared_ptr<Stream>& stream)
    {
        streams.erase(std::remove(streams.begin(), streams.end(), stream), streams.end());
    }
}
