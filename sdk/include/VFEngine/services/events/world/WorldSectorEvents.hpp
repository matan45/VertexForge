#pragma once

#include "../EventTypes.hpp"
#include "world/WorldTypes.hpp"
#include <glm/glm.hpp>
#include <string>
#include <optional>
#include <vector>

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

    // Marks the sector owning the given entity as needing save. Editor code should
    // execute this after mutating components on a sector-managed entity (transform
    // moves are tracked automatically via TransformChangedNotification).
    struct MarkEntitySectorDirtyCommand : ICommand<>
    {
        uint64_t entityUUID = 0;

        std::string_view getName() const override { return "MarkEntitySectorDirty"; }
    };

    struct RegisterStreamingSourceCommand : ICommand<uint32_t>
    {
        glm::vec3 position{0.0f};
        float radiusMultiplier = 1.0f;
        uint8_t priority = 0;
        // Optional owning entity: the source auto-unregisters when this entity is deleted
        uint64_t ownerEntityUUID = 0;

        std::string_view getName() const override { return "RegisterStreamingSource"; }
    };

    struct UnregisterStreamingSourceCommand : ICommand<>
    {
        uint32_t sourceId = 0;

        std::string_view getName() const override { return "UnregisterStreamingSource"; }
    };

    struct UpdateStreamingSourcePositionCommand : ICommand<>
    {
        uint32_t sourceId = 0;
        glm::vec3 position{0.0f};

        std::string_view getName() const override { return "UpdateStreamingSourcePosition"; }
    };

    struct IsStreamingSourceValidQuery : IQuery<bool>
    {
        uint32_t sourceId = 0;

        std::string_view getName() const override { return "IsStreamingSourceValid"; }
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

    // Live-updates the streaming configuration (streamer + HLOD streamer + world
    // definition). Persisted on the next Save World.
    struct SetStreamingConfigCommand : ICommand<>
    {
        ::world::SectorStreamingConfig config;

        std::string_view getName() const override { return "SetStreamingConfig"; }
    };

    struct GetSectorConfigQuery : IQuery<::world::SectorConfig>
    {
        std::string_view getName() const override { return "GetSectorConfig"; }
    };

    struct GetLoadedSectorCoordsQuery : IQuery<std::vector<::world::SectorCoord>>
    {
        std::string_view getName() const override { return "GetLoadedSectorCoords"; }
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

    struct SectorAboutToLoadNotification : INotification
    {
        ::world::SectorCoord coord;
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
        ::world::SectorConfig sectorConfig;

        std::string_view getName() const override { return "SectorAboutToLoad"; }
    };

    struct SectorActivatedNotification : INotification
    {
        ::world::SectorCoord coord;
        ::world::SectorConfig sectorConfig;

        std::string_view getName() const override { return "SectorActivated"; }
    };

    struct SectorDeactivatedNotification : INotification
    {
        ::world::SectorCoord coord;
        ::world::SectorConfig sectorConfig;

        std::string_view getName() const override { return "SectorDeactivated"; }
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
        ::world::SectorConfig sectorConfig;

        std::string_view getName() const override { return "SectorUnloaded"; }
    };

} // namespace events::world
