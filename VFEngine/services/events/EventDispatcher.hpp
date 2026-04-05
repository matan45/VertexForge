#pragma once
#include "print/Log.hpp"
#include "EventTypes.hpp"
#include <any>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace events {

    class EventDispatcher {
    public:
        static EventDispatcher& instance();

        EventDispatcher(const EventDispatcher&) = delete;
        EventDispatcher& operator=(const EventDispatcher&) = delete;

        template<typename TCommand>
        typename TCommand::ResultType execute(const TCommand& command);

        template<typename TQuery>
        typename TQuery::ResultType query(const TQuery& queryObj);

        template<typename TNotification>
        void publish(const TNotification& notification);

        template<typename TCommand>
        void registerCommandHandler(std::function<typename TCommand::ResultType(const TCommand&)> handler);

        template<typename TQuery>
        void registerQueryHandler(std::function<typename TQuery::ResultType(const TQuery&)> handler);

        template<typename TCommand>
        void unregisterCommandHandler();

        template<typename TQuery>
        void unregisterQueryHandler();

        template<typename TNotification>
        SubscriptionToken subscribe(std::function<void(const TNotification&)> handler);

        void unsubscribe(SubscriptionToken token);

        void clear();

    private:
        EventDispatcher() = default;
        ~EventDispatcher() = default;

        std::unordered_map<std::type_index, std::any> commandHandlers;
        std::unordered_map<std::type_index, std::any> queryHandlers;

        struct SubscriberEntry {
            SubscriptionToken token;
            std::any handler;
        };
        std::unordered_map<std::type_index, std::vector<SubscriberEntry>> notificationSubscribers;

        std::atomic<uint64_t> nextToken{ 1 };
        mutable std::shared_mutex mutex;
    };

    // Lock strategy: lookup under lock, release lock, then invoke.
    // This avoids recursive locking when handlers call back into the dispatcher
    // (e.g. execute -> handler -> publish -> handler -> execute).

    template<typename TCommand>
    typename TCommand::ResultType EventDispatcher::execute(const TCommand& command) {
        std::function<typename TCommand::ResultType(const TCommand&)> handler;
        {
            std::shared_lock lock(mutex);
            auto it = commandHandlers.find(std::type_index(typeid(TCommand)));
            if (it == commandHandlers.end()) {
                throw std::runtime_error(std::string("No handler registered for command: ") + std::string(command.getName()));
            }
            using HandlerType = std::function<typename TCommand::ResultType(const TCommand&)>;
            handler = std::any_cast<const HandlerType&>(it->second);
        }
        return handler(command);
    }

    template<typename TQuery>
    typename TQuery::ResultType EventDispatcher::query(const TQuery& queryObj) {
        std::function<typename TQuery::ResultType(const TQuery&)> handler;
        {
            std::shared_lock lock(mutex);
            auto it = queryHandlers.find(std::type_index(typeid(TQuery)));
            if (it == queryHandlers.end()) {
                throw std::runtime_error(std::string("No handler registered for query: ") + std::string(queryObj.getName()));
            }
            using HandlerType = std::function<typename TQuery::ResultType(const TQuery&)>;
            handler = std::any_cast<const HandlerType&>(it->second);
        }
        return handler(queryObj);
    }

    template<typename TNotification>
    void EventDispatcher::publish(const TNotification& notification) {
        std::vector<SubscriberEntry> subscribers;
        {
            std::shared_lock lock(mutex);
            auto it = notificationSubscribers.find(std::type_index(typeid(TNotification)));
            if (it == notificationSubscribers.end()) {
                return;
            }
            subscribers = it->second;
        }

        using HandlerType = std::function<void(const TNotification&)>;
        for (const auto& entry : subscribers) {
            try {
                const auto& handler = std::any_cast<const HandlerType&>(entry.handler);
                handler(notification);
            }
            catch (const std::exception&) {
            }
        }
    }

    template<typename TCommand>
    void EventDispatcher::registerCommandHandler(std::function<typename TCommand::ResultType(const TCommand&)> handler) {
        std::unique_lock lock(mutex);
        auto typeIdx = std::type_index(typeid(TCommand));
        if (commandHandlers.contains(typeIdx)) {
            vfLogWarning("Command handler for '{}' is being replaced. This may indicate duplicate registration.", typeid(TCommand).name());
        }
        commandHandlers[typeIdx] = std::move(handler);
    }

    template<typename TQuery>
    void EventDispatcher::registerQueryHandler(std::function<typename TQuery::ResultType(const TQuery&)> handler) {
        std::unique_lock lock(mutex);
        auto typeIdx = std::type_index(typeid(TQuery));
        if (queryHandlers.contains(typeIdx)) {
            vfLogWarning("Query handler for '{}' is being replaced. This may indicate duplicate registration.", typeid(TQuery).name());
        }
        queryHandlers[typeIdx] = std::move(handler);
    }

    template<typename TCommand>
    void EventDispatcher::unregisterCommandHandler() {
        std::unique_lock lock(mutex);
        commandHandlers.erase(std::type_index(typeid(TCommand)));
    }

    template<typename TQuery>
    void EventDispatcher::unregisterQueryHandler() {
        std::unique_lock lock(mutex);
        queryHandlers.erase(std::type_index(typeid(TQuery)));
    }

    template<typename TNotification>
    SubscriptionToken EventDispatcher::subscribe(std::function<void(const TNotification&)> handler) {
        std::unique_lock lock(mutex);

        SubscriptionToken token{ nextToken++ };

        auto& subscribers = notificationSubscribers[std::type_index(typeid(TNotification))];
        subscribers.push_back({ token, std::move(handler) });

        return token;
    }

    class ScopedSubscription
    {
    public:
        ScopedSubscription() = default;

        explicit ScopedSubscription(SubscriptionToken token)
            : token(token)
        {
        }

        ScopedSubscription(const ScopedSubscription&) = delete;
        ScopedSubscription& operator=(const ScopedSubscription&) = delete;

        ScopedSubscription(ScopedSubscription&& other) noexcept
            : token(other.token)
        {
            other.token = {};
        }

        ScopedSubscription& operator=(ScopedSubscription&& other) noexcept
        {
            if (this != &other)
            {
                unsubscribe();
                token = other.token;
                other.token = {};
            }
            return *this;
        }

        ~ScopedSubscription()
        {
            unsubscribe();
        }

        void unsubscribe()
        {
            if (token.isValid())
            {
                EventDispatcher::instance().unsubscribe(token);
                token = {};
            }
        }

        bool isValid() const { return token.isValid(); }

    private:
        SubscriptionToken token;
    };

}
