#pragma once
#include "events/EventTypes.hpp"
#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include <atomic>

namespace plugin
{
    class PluginEventBus
    {
    public:
        static PluginEventBus& instance();

        events::SubscriptionToken subscribe(const std::string& eventName,
                                             std::function<void(const nlohmann::json&)> handler);

        void unsubscribe(events::SubscriptionToken token);

        void publish(const std::string& eventName, const nlohmann::json& data);

    private:
        PluginEventBus() = default;

        struct Subscriber
        {
            events::SubscriptionToken token;
            std::function<void(const nlohmann::json&)> handler;
        };

        mutable std::shared_mutex mutex;
        std::unordered_map<std::string, std::vector<Subscriber>> subscribers;
        std::unordered_map<uint64_t, std::string> tokenToEvent; // reverse index
        std::atomic<uint64_t> nextTokenId{1};
    };
}
