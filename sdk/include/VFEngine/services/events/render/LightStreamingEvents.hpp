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

    // VK-1599: `sectorId` is world::sectorRegistrationId(gridIndex, coord). 64 bits because the
    // packed coord alone fills 32 and a world may run several streaming grids.
    struct RegisterSectorLightsCommand : ::events::ICommand<void>
    {
        uint64_t sectorId;
        std::vector<uint32_t> lightEntityIds;
        std::string_view getName() const override { return "RegisterSectorLights"; }
    };

    struct UnregisterSectorLightsCommand : ::events::ICommand<void>
    {
        uint64_t sectorId;
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

    // ---- Shadow Streaming ----

    struct ShadowStreamingStats
    {
        uint32_t pagesAllocatedThisFrame = 0;
        uint32_t pagesEvictedThisFrame = 0;
        uint32_t pendingPages = 0;
        float poolUtilization = 0.0f;
    };

    struct SetShadowStreamingConfigCommand : ::events::ICommand<>
    {
        uint32_t maxNewPagesPerFrame = 32;
        std::string_view getName() const override { return "SetShadowStreamingConfig"; }
    };

    struct GetShadowStreamingStatsQuery : ::events::IQuery<ShadowStreamingStats>
    {
        std::string_view getName() const override { return "GetShadowStreamingStats"; }
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
