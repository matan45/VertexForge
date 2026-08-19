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

    // VK-1594: bakes EVERY configured tier, asynchronously on the JobSystem. Returns whether the
    // bake was accepted and started, not whether it finished - poll GetHLODBakeProgressQuery.
    struct GenerateAllHLODCommand : ICommand<bool>
    {
        // Skip cells that already have a bake recorded. Covers the "Generate Missing" case,
        // including cells invalidated by a dirty-sector save.
        bool missingOnly = false;

        std::string_view getName() const override { return "GenerateAllHLOD"; }
    };

    struct CancelHLODBakeCommand : ICommand<>
    {
        std::string_view getName() const override { return "CancelHLODBake"; }
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

    // VK-1594: async bake progress, polled by the editor's HLOD tab.
    struct HLODBakeProgress
    {
        bool running = false;
        bool cancelled = false;
        uint32_t cellsDone = 0;
        uint32_t cellsTotal = 0;
        uint32_t cellsFailed = 0;
        uint8_t currentTier = 0;

        [[nodiscard]] float fraction() const
        {
            return cellsTotal > 0 ? static_cast<float>(cellsDone) / static_cast<float>(cellsTotal)
                                  : 0.0f;
        }
    };

    struct GetHLODBakeProgressQuery : IQuery<HLODBakeProgress>
    {
        std::string_view getName() const override { return "GetHLODBakeProgress"; }
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
