#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "vegetation/VegetationSpecies.hpp"

#include <string>
#include <vector>
#include <unordered_map>

namespace events::vegetation
{
    // --- Species Management ---

    struct AddVegetationSpeciesCommand : ICommand<uint32_t>
    {
        ::vegetation::VegetationSpeciesConfig config;

        std::string_view getName() const override { return "AddVegetationSpecies"; }
    };

    struct RemoveVegetationSpeciesCommand : ICommand<void>
    {
        uint32_t speciesId;

        std::string_view getName() const override { return "RemoveVegetationSpecies"; }
    };

    struct UpdateVegetationSpeciesCommand : ICommand<void>
    {
        uint32_t speciesId;
        ::vegetation::VegetationSpeciesConfig config;

        std::string_view getName() const override { return "UpdateVegetationSpecies"; }
    };

    struct GetVegetationSpeciesQuery : IQuery<::vegetation::VegetationSpeciesConfig>
    {
        uint32_t speciesId;

        std::string_view getName() const override { return "GetVegetationSpecies"; }
    };

    struct GetAllVegetationSpeciesQuery : IQuery<std::unordered_map<uint32_t, ::vegetation::VegetationSpeciesConfig>>
    {
        std::string_view getName() const override { return "GetAllVegetationSpecies"; }
    };

    struct GetVegetationSpeciesCountQuery : IQuery<uint32_t>
    {
        std::string_view getName() const override { return "GetVegetationSpeciesCount"; }
    };

    struct ClearAllVegetationSpeciesCommand : ICommand<void>
    {
        std::string_view getName() const override { return "ClearAllVegetationSpecies"; }
    };

    // --- Vegetation Configuration ---

    struct SetVegetationEnabledCommand : ICommand<void>
    {
        services::EntityHandle entityId;
        bool enabled;

        std::string_view getName() const override { return "SetVegetationEnabled"; }
    };

    // --- Notifications ---

    struct VegetationSpeciesChangedNotification : INotification
    {
        uint32_t speciesId;

        std::string_view getName() const override { return "VegetationSpeciesChanged"; }
    };

    struct VegetationUpdatedNotification : INotification
    {
        std::string_view getName() const override { return "VegetationUpdated"; }
    };
}
