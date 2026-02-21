#pragma once
#include "EventTypes.hpp"
#include "../data/EntityHandle.hpp"
#include <string>

namespace events::animation
{
    struct AnimationEventFiredNotification : ::events::INotification
    {
        ::services::EntityHandle entity;
        std::string eventName;
        std::string stateName;
        std::string payload;

        std::string_view getName() const override { return "AnimationEventFired"; }
    };
}
