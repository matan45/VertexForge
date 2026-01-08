#pragma once
#include <functional>
#include <string_view>

namespace events
{
    struct IEvent
    {
        virtual ~IEvent() = default;
        virtual std::string_view getName() const = 0;
    };

    template <typename TResult = void>
    struct ICommand : IEvent
    {
        using ResultType = TResult;
    };


    template <typename TResult>
    struct IQuery : IEvent
    {
        using ResultType = TResult;
    };

    struct INotification : IEvent
    {
    };

    struct SubscriptionToken
    {
        uint64_t id = 0;

        bool isValid() const { return id != 0; }
        bool operator==(const SubscriptionToken& other) const { return id == other.id; }
        bool operator!=(const SubscriptionToken& other) const { return id != other.id; }

        struct Hash
        {
            size_t operator()(const SubscriptionToken& token) const
            {
                return std::hash<uint64_t>{}(token.id);
            }
        };
    };
}
