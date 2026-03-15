#include "CoroutineManager.hpp"
#include <algorithm>

namespace core
{
    value::Value CoroutineManager::waitForSeconds(uint64_t instanceId, double seconds)
    {
        auto promise = std::make_shared<value::AsyncPromiseValue>();

        PendingWait wait;
        wait.id = nextWaitId++;
        wait.scriptInstanceId = instanceId;
        wait.promise = promise;
        wait.type = WaitType::Seconds;
        wait.resolveTime = elapsedTime + seconds;

        pendingWaits.push_back(std::move(wait));
        return value::Value(std::static_pointer_cast<value::PromiseValue>(promise));
    }

    value::Value CoroutineManager::waitForFrames(uint64_t instanceId, int frames)
    {
        auto promise = std::make_shared<value::AsyncPromiseValue>();

        PendingWait wait;
        wait.id = nextWaitId++;
        wait.scriptInstanceId = instanceId;
        wait.promise = promise;
        wait.type = WaitType::Frames;
        wait.framesRemaining = frames;

        pendingWaits.push_back(std::move(wait));
        return value::Value(std::static_pointer_cast<value::PromiseValue>(promise));
    }

    value::Value CoroutineManager::waitForFixedUpdate(uint64_t instanceId)
    {
        auto promise = std::make_shared<value::AsyncPromiseValue>();

        PendingWait wait;
        wait.id = nextWaitId++;
        wait.scriptInstanceId = instanceId;
        wait.promise = promise;
        wait.type = WaitType::FixedUpdate;

        pendingWaits.push_back(std::move(wait));
        return value::Value(std::static_pointer_cast<value::PromiseValue>(promise));
    }

    void CoroutineManager::tickFrame(double deltaTime)
    {
        elapsedTime += deltaTime;

        auto it = pendingWaits.begin();
        while (it != pendingWaits.end())
        {
            bool resolve = false;

            switch (it->type)
            {
            case WaitType::Seconds:
                resolve = (elapsedTime >= it->resolveTime);
                break;
            case WaitType::Frames:
                it->framesRemaining--;
                resolve = (it->framesRemaining <= 0);
                break;
            case WaitType::FixedUpdate:
                // Resolved only in tickFixedUpdate
                break;
            }

            if (resolve)
            {
                it->promise->resolve(value::Value(std::monostate{}));
                it = pendingWaits.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void CoroutineManager::tickFixedUpdate()
    {
        auto it = pendingWaits.begin();
        while (it != pendingWaits.end())
        {
            if (it->type == WaitType::FixedUpdate)
            {
                it->promise->resolve(value::Value(std::monostate{}));
                it = pendingWaits.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void CoroutineManager::removeAllForInstance(uint64_t instanceId)
    {
        pendingWaits.erase(
            std::remove_if(pendingWaits.begin(), pendingWaits.end(),
                           [instanceId](const PendingWait& w) { return w.scriptInstanceId == instanceId; }),
            pendingWaits.end());
    }

    void CoroutineManager::clear()
    {
        pendingWaits.clear();
        elapsedTime = 0.0;
    }
}
