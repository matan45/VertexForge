#include "PluginEventBus.hpp"

namespace plugin
{
    PluginEventBus& PluginEventBus::instance()
    {
        static PluginEventBus bus;
        return bus;
    }

    events::SubscriptionToken PluginEventBus::subscribe(const std::string& eventName,
                                                         std::function<void(const nlohmann::json&)> handler)
    {
        std::unique_lock lock(mutex);

        events::SubscriptionToken token;
        token.id = nextTokenId++;

        subscribers[eventName].push_back({token, std::move(handler)});
        return token;
    }

    void PluginEventBus::unsubscribe(events::SubscriptionToken token)
    {
        std::unique_lock lock(mutex);

        for (auto& [eventName, subs] : subscribers)
        {
            std::erase_if(subs, [&](const Subscriber& s)
            {
                return s.token == token;
            });
        }
    }

    void PluginEventBus::publish(const std::string& eventName, const nlohmann::json& data)
    {
        std::shared_lock lock(mutex);

        auto it = subscribers.find(eventName);
        if (it == subscribers.end())
            return;

        // Copy handlers to avoid holding lock during callbacks
        auto handlers = it->second;
        lock.unlock();

        for (const auto& sub : handlers)
        {
            sub.handler(data);
        }
    }
}
