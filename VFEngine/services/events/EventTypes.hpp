#pragma once
#include <functional>
#include <string_view>

namespace events {

    // Base interface for all events
    struct IEvent {
        virtual ~IEvent() = default;
        virtual std::string_view getName() const = 0;
    };

    // Commands - Request an action, optionally returns a result
    // Use for operations that change state
    template<typename TResult = void>
    struct ICommand : IEvent {
        using ResultType = TResult;
    };

    // Queries - Request data, always returns a result
    // Use for read-only operations
    template<typename TResult>
    struct IQuery : IEvent {
        using ResultType = TResult;
    };

    // Notifications - Inform about state changes (broadcast)
    // Use for reactive updates, no return value
    struct INotification : IEvent {};

    // Subscription token for unsubscribing from notifications
    struct SubscriptionToken {
        uint64_t id = 0;

        bool isValid() const { return id != 0; }
        bool operator==(const SubscriptionToken& other) const { return id == other.id; }
        bool operator!=(const SubscriptionToken& other) const { return id != other.id; }

        struct Hash {
            size_t operator()(const SubscriptionToken& token) const {
                return std::hash<uint64_t>{}(token.id);
            }
        };
    };

}
