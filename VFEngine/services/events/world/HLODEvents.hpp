#pragma once

#include "../EventTypes.hpp"
#include "world/HLODTypes.hpp"
#include "world/WorldTypes.hpp"
#include <string>

namespace events::world::hlod
{
    // ============================================
    // Commands
    // ============================================

    struct GenerateHLODCommand : ICommand<bool>
    {
        ::world::SectorCoord coord;
        uint8_t tier = 0;

        std::string_view getName() const override { return "GenerateHLOD"; }
    };

    struct GenerateAllHLODCommand : ICommand<bool>
    {
        std::string_view getName() const override { return "GenerateAllHLOD"; }
    };

    struct SetHLODConfigCommand : ICommand<>
    {
        ::world::HLODConfig config;

        std::string_view getName() const override { return "SetHLODConfig"; }
    };

    struct InvalidateHLODCommand : ICommand<>
    {
        ::world::SectorCoord coord;

        std::string_view getName() const override { return "InvalidateHLOD"; }
    };

    // ============================================
    // Queries
    // ============================================

    struct GetHLODConfigQuery : IQuery<::world::HLODConfig>
    {
        std::string_view getName() const override { return "GetHLODConfig"; }
    };

    struct GetHLODStatusQuery : IQuery<::world::HLODProxyInfo>
    {
        ::world::HLODCellCoord coord;

        std::string_view getName() const override { return "GetHLODStatus"; }
    };

    struct IsHLODGeneratedQuery : IQuery<bool>
    {
        ::world::SectorCoord coord;

        std::string_view getName() const override { return "IsHLODGenerated"; }
    };

    // ============================================
    // Notifications
    // ============================================

    struct HLODGenerationCompleteNotification : INotification
    {
        ::world::HLODCellCoord coord;
        bool success = false;

        std::string_view getName() const override { return "HLODGenerationComplete"; }
    };

    struct HLODProxyLoadedNotification : INotification
    {
        ::world::HLODCellCoord coord;

        std::string_view getName() const override { return "HLODProxyLoaded"; }
    };

    struct HLODProxyUnloadedNotification : INotification
    {
        ::world::HLODCellCoord coord;

        std::string_view getName() const override { return "HLODProxyUnloaded"; }
    };

    // Published when a sector's baked HLOD becomes stale (sector content changed)
    // and its .vfHLOD file was discarded. Regenerate via GenerateHLODCommand.
    struct HLODInvalidatedNotification : INotification
    {
        ::world::SectorCoord coord;

        std::string_view getName() const override { return "HLODInvalidated"; }
    };

} // namespace events::world::hlod
