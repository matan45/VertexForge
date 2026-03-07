#pragma once

#include "../EventTypes.hpp"
#include "world/WorldTypes.hpp"
#include <glm/glm.hpp>
#include <string>
#include <optional>

namespace events::world
{
    // ============================================
    // Commands
    // ============================================

    struct CreateWorldCommand : ICommand<bool>
    {
        std::string name;
        std::string filePath;
        ::world::SectorConfig sectorConfig;
        ::world::SectorStreamingConfig streamingConfig;

        std::string_view getName() const override { return "CreateWorld"; }
    };

    struct SaveWorldCommand : ICommand<bool>
    {
        std::string filePath;

        std::string_view getName() const override { return "SaveWorld"; }
    };

    struct LoadWorldCommand : ICommand<bool>
    {
        std::string filePath;

        std::string_view getName() const override { return "LoadWorld"; }
    };

    struct SaveSectorCommand : ICommand<bool>
    {
        ::world::SectorCoord coord;
        std::string filePath;

        std::string_view getName() const override { return "SaveSector"; }
    };

    struct LoadSectorCommand : ICommand<bool>
    {
        ::world::SectorCoord coord;

        std::string_view getName() const override { return "LoadSector"; }
    };

    struct UnloadSectorCommand : ICommand<bool>
    {
        ::world::SectorCoord coord;

        std::string_view getName() const override { return "UnloadSector"; }
    };

    struct ClearWorldCommand : ICommand<>
    {
        std::string_view getName() const override { return "ClearWorld"; }
    };

    struct UpdateWorldStreamingCommand : ICommand<>
    {
        glm::vec3 cameraPos{0.0f};

        std::string_view getName() const override { return "UpdateWorldStreaming"; }
    };

    // ============================================
    // Queries
    // ============================================

    struct GetSectorAtPositionQuery : IQuery<std::optional<::world::SectorCoord>>
    {
        glm::vec3 position{0.0f};

        std::string_view getName() const override { return "GetSectorAtPosition"; }
    };

    struct GetSectorStateQuery : IQuery<::world::SectorState>
    {
        ::world::SectorCoord coord;

        std::string_view getName() const override { return "GetSectorState"; }
    };

    struct DoesSectorExistQuery : IQuery<bool>
    {
        ::world::SectorCoord coord;

        std::string_view getName() const override { return "DoesSectorExist"; }
    };

    struct IsWorldModeQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "IsWorldMode"; }
    };

    struct GetWorldStreamingStatsQuery : IQuery<::world::SectorStreamingConfig>
    {
        std::string_view getName() const override { return "GetWorldStreamingStats"; }
    };

    struct SetSectorDebugDrawCommand : ICommand<>
    {
        bool enabled = false;

        std::string_view getName() const override { return "SetSectorDebugDraw"; }
    };

    struct GetSectorDebugDrawQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "GetSectorDebugDraw"; }
    };

    // ============================================
    // Notifications
    // ============================================

    struct WorldLoadedNotification : INotification
    {
        std::string worldPath;

        std::string_view getName() const override { return "WorldLoaded"; }
    };

    struct SectorLoadedNotification : INotification
    {
        ::world::SectorCoord coord;
        uint32_t entityCount = 0;

        std::string_view getName() const override { return "SectorLoaded"; }
    };

    struct SectorUnloadedNotification : INotification
    {
        ::world::SectorCoord coord;

        std::string_view getName() const override { return "SectorUnloaded"; }
    };

} // namespace events::world
