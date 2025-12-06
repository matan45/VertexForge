#include "EventDispatcher.hpp"
#include <algorithm>

namespace events {

    EventDispatcher& EventDispatcher::instance() {
        static EventDispatcher instance;
        return instance;
    }

    void EventDispatcher::unsubscribe(SubscriptionToken token) {
        if (!token.isValid()) {
            return;
        }

        std::unique_lock lock(mutex);

        for (auto& [typeIndex, subscribers] : notificationSubscribers) {
            auto it = std::remove_if(subscribers.begin(), subscribers.end(),
                [token](const SubscriberEntry& entry) {
                    return entry.token == token;
                });

            if (it != subscribers.end()) {
                subscribers.erase(it, subscribers.end());
                return;
            }
        }
    }

    void EventDispatcher::clear() {
        std::unique_lock lock(mutex);
        commandHandlers.clear();
        queryHandlers.clear();
        notificationSubscribers.clear();
    }

}
