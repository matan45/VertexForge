#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include <cstdint>

namespace events::lighting
{
    enum class LightType : uint8_t
    {
        Directional = 0,
        Point = 1,
        Spot = 2
    };

    struct LightDataChangedNotification : INotification
    {
        services::EntityHandle entity;
        LightType lightType;

        std::string_view getName() const override { return "LightDataChanged"; }
    };

    struct LightComponentChangedNotification : INotification
    {
        services::EntityHandle entity;
        LightType lightType;
        bool added;

        std::string_view getName() const override { return "LightComponentChanged"; }
    };
}
