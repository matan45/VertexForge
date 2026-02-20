#pragma once
#include "EventTypes.hpp"
#include "navigation/NavmeshData.hpp"
#include "types/NavmeshTypes.hpp"
#include "../data/EntityHandle.hpp"
#include <glm/glm.hpp>
#include <string>

namespace events::navmesh
{
    // === Commands ===

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

    // === Queries ===

    struct FindPathQuery : IQuery<navigation::NavPath>
    {
        glm::vec3 start;
        glm::vec3 end;
        float agentRadius = 0.3f;
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

    // === Notifications ===

    struct NavmeshBakeCompleteNotification : INotification
    {
        bool success = false;
        std::string message;
        std::string_view getName() const override { return "NavmeshBakeComplete"; }
    };
}
