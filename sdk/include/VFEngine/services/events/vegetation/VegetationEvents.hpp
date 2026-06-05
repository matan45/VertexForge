#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"

#include <string>

namespace events::vegetation
{
    // --- Vegetation Configuration ---

    struct SetVegetationEnabledCommand : ICommand<void>
    {
        services::EntityHandle entityId;
        bool enabled;

        std::string_view getName() const override { return "SetVegetationEnabled"; }
    };

    // --- Notifications ---

    struct VegetationUpdatedNotification : INotification
    {
        std::string_view getName() const override { return "VegetationUpdated"; }
    };
}
