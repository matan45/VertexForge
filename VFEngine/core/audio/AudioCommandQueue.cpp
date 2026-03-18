#include "AudioCommandQueue.hpp"

namespace core::audio
{
    void AudioCommandQueue::enqueue(AudioCommand cmd)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            queue.push_back(std::move(cmd));
        }
        cv.notify_one();
    }

    bool AudioCommandQueue::tryDequeueAll(std::vector<AudioCommand>& out)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty()) return false;

        out.reserve(out.size() + queue.size());
        for (auto& cmd : queue)
        {
            out.push_back(std::move(cmd));
        }
        queue.clear();
        return true;
    }

    void AudioCommandQueue::waitForCommands(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait_for(lock, timeout, [this]() { return !queue.empty(); });
    }

    void AudioCommandQueue::notify()
    {
        cv.notify_one();
    }
}
