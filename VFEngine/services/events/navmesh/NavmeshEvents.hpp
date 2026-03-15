#pragma once
#include "../EventTypes.hpp"
#include "navigation/NavmeshData.hpp"
#include "types/NavmeshTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include <glm/glm.hpp>
#include <string>

namespace events::navmesh
{

    struct BakeNavmeshCommand : ICommand<>
    {
        types::NavmeshBakeSettings settings;
        std::string_view getName() const override { return "BakeNavmesh"; }
    };

    struct SaveNavmeshCommand : ICommand<bool>
    {
        std::string filePath;
        std::string_view getName() const override { return "SaveNavmesh"; }
    };

    struct LoadNavmeshCommand : ICommand<bool>
    {
        std::string filePath;
        std::string_view getName() const override { return "LoadNavmesh"; }
    };

    struct ClearNavmeshCommand : ICommand<>
    {
        std::string_view getName() const override { return "ClearNavmesh"; }
    };

    struct AddAgentCommand : ICommand<>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "AddNavmeshAgent"; }
    };

    struct RemoveAgentCommand : ICommand<>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "RemoveNavmeshAgent"; }
    };

    struct SetAgentDestinationCommand : ICommand<>
    {
        services::EntityHandle entity;
        glm::vec3 target;
        std::string_view getName() const override { return "SetAgentDestination"; }
    };

    struct StopAgentCommand : ICommand<>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "StopNavmeshAgent"; }
    };


    struct UpdateAgentConfigCommand : ICommand<>
    {
        services::EntityHandle entity;
        float maxSpeed = -1.0f;
        float maxAcceleration = -1.0f;
        std::string_view getName() const override { return "UpdateAgentConfig"; }
    };

    struct GetAgentVelocityQuery : IQuery<glm::vec3>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetAgentVelocity"; }
    };

    struct NavmeshRaycastQuery : IQuery<navigation::NavmeshRaycastResult>
    {
        glm::vec3 from;
        glm::vec3 to;
        std::string_view getName() const override { return "NavmeshRaycast"; }
    };

    struct AgentReachedDestinationNotification : INotification
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "AgentReachedDestination"; }
    };

    struct AgentPathBlockedNotification : INotification
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "AgentPathBlocked"; }
    };

    struct FindPathQuery : IQuery<navigation::NavPath>
    {
        glm::vec3 start;
        glm::vec3 end;
        float agentRadius = 0.25f;
        float agentHeight = 2.0f;
        std::string_view getName() const override { return "FindPath"; }
    };

    struct GetClosestPointQuery : IQuery<glm::vec3>
    {
        glm::vec3 point;
        float searchRadius = 5.0f;
        std::string_view getName() const override { return "GetClosestPointOnNavmesh"; }
    };

    struct IsPointOnNavmeshQuery : IQuery<bool>
    {
        glm::vec3 point;
        float tolerance = 0.5f;
        std::string_view getName() const override { return "IsPointOnNavmesh"; }
    };

    struct HasNavmeshQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "HasNavmesh"; }
    };

    struct GetBakeProgressQuery : IQuery<types::NavmeshBakeProgress>
    {
        std::string_view getName() const override { return "GetNavmeshBakeProgress"; }
    };

    struct GetNavmeshSettingsQuery : IQuery<types::NavmeshBakeSettings>
    {
        std::string_view getName() const override { return "GetNavmeshSettings"; }
    };

    struct NavmeshDebugMeshResult
    {
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;
    };

    struct GetNavmeshDebugMeshQuery : IQuery<NavmeshDebugMeshResult>
    {
        std::string_view getName() const override { return "GetNavmeshDebugMesh"; }
    };


    struct NavmeshBakeCompleteNotification : INotification
    {
        bool success = false;
        std::string message;
        std::string_view getName() const override { return "NavmeshBakeComplete"; }
    };

    struct BakeTileCommand : ICommand<bool>
    {
        int tileX = 0;
        int tileZ = 0;
        std::string_view getName() const override { return "BakeTile"; }
    };

    struct BakeAllTilesCommand : ICommand<>
    {
        types::NavmeshBakeSettings settings;
        std::string_view getName() const override { return "BakeAllTiles"; }
    };

    struct SaveNavmeshTiledCommand : ICommand<bool>
    {
        std::string directory;
        std::string_view getName() const override { return "SaveNavmeshTiled"; }
    };

    struct LoadNavmeshTiledCommand : ICommand<bool>
    {
        std::string directory;
        std::string_view getName() const override { return "LoadNavmeshTiled"; }
    };

    struct NavmeshStreamingConfig
    {
        float loadRadius = 512.0f;
        float unloadRadius = 640.0f;
        int maxLoadsPerFrame = 2;
        int maxUnloadsPerFrame = 2;
    };

    struct SetNavmeshStreamingConfigCommand : ICommand<>
    {
        NavmeshStreamingConfig config;
        std::string_view getName() const override { return "SetNavmeshStreamingConfig"; }
    };

    struct SetNavmeshStreamingEnabledCommand : ICommand<>
    {
        bool enabled = false;
        std::string_view getName() const override { return "SetNavmeshStreamingEnabled"; }
    };

    struct GetNavmeshStreamingConfigQuery : IQuery<NavmeshStreamingConfig>
    {
        std::string_view getName() const override { return "GetNavmeshStreamingConfig"; }
    };

    struct IsNavmeshStreamingEnabledQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "IsNavmeshStreamingEnabled"; }
    };

    enum class NavmeshTileStatus : uint8_t
    {
        NotBaked = 0,
        Baked,
        Loaded,
        Dirty,
        Baking
    };

    struct NavmeshTileStatusInfo
    {
        navigation::NavmeshTileCoord coord;
        NavmeshTileStatus status = NavmeshTileStatus::NotBaked;
    };

    struct GetNavmeshTileStatusQuery : IQuery<std::vector<NavmeshTileStatusInfo>>
    {
        std::string_view getName() const override { return "GetNavmeshTileStatus"; }
    };

    struct NavmeshTileLoadedNotification : INotification
    {
        int tileX = 0;
        int tileZ = 0;
        std::string_view getName() const override { return "NavmeshTileLoaded"; }
    };

    struct NavmeshTileUnloadedNotification : INotification
    {
        int tileX = 0;
        int tileZ = 0;
        std::string_view getName() const override { return "NavmeshTileUnloaded"; }
    };

    struct NavmeshTileUpdatedNotification : INotification
    {
        int tileX = 0;
        int tileZ = 0;
        std::string_view getName() const override { return "NavmeshTileUpdated"; }
    };
}
