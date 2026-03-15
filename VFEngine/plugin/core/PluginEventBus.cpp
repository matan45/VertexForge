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
        tokenToEvent[token.id] = eventName;
        return token;
    }

    void PluginEventBus::unsubscribe(events::SubscriptionToken token)
    {
        std::unique_lock lock(mutex);

        auto it = tokenToEvent.find(token.id);
        if (it == tokenToEvent.end())
            return;

        auto& subs = subscribers[it->second];
        std::erase_if(subs, [&](const Subscriber& s) { return s.token == token; });

        tokenToEvent.erase(it);
    }

    void PluginEventBus::publish(const std::string& eventName, const nlohmann::json& data)
    {
        std::vector<Subscriber> handlers;
        {
            std::shared_lock lock(mutex);
            auto it = subscribers.find(eventName);
            if (it == subscribers.end())
                return;
            handlers = it->second;
        }

        for (const auto& sub : handlers)
        {
            sub.handler(data);
        }
    }
}
