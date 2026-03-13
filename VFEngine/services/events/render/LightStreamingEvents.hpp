#pragma once
#include "../EventTypes.hpp"
#include "../../../graphics/render/lighting/LightStreamManager.hpp"
#include <cstdint>
#include <vector>

namespace events::render::lightstreaming
{
    // ---- Commands ----

    struct SetLightStreamingConfigCommand : ::events::ICommand<void>
    {
        ::render::lighting::LightStreamingConfig config;
        std::string_view getName() const override { return "SetLightStreamingConfig"; }
    };

    struct RegisterSectorLightsCommand : ::events::ICommand<void>
    {
        uint32_t sectorId;
        std::vector<uint32_t> lightEntityIds;
        std::string_view getName() const override { return "RegisterSectorLights"; }
    };

    struct UnregisterSectorLightsCommand : ::events::ICommand<void>
    {
        uint32_t sectorId;
        std::string_view getName() const override { return "UnregisterSectorLights"; }
    };

    // ---- Queries ----

    struct GetLightStreamingStatsQuery : ::events::IQuery<::render::lighting::LightStreamingStats>
    {
        std::string_view getName() const override { return "GetLightStreamingStats"; }
    };

    struct GetLightStreamingConfigQuery : ::events::IQuery<::render::lighting::LightStreamingConfig>
    {
        std::string_view getName() const override { return "GetLightStreamingConfig"; }
    };

    // ---- Notifications ----

    struct LightBudgetExceededNotification : ::events::INotification
    {
        uint32_t requestedCount;
        uint32_t budgetCount;
        uint32_t excludedCount;
        std::string_view getName() const override { return "LightBudgetExceeded"; }
    };
}
