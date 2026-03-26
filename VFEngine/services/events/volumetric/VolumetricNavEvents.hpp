#pragma once
#include "../EventTypes.hpp"
#include "navigation/volumetric/VolumetricTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include <glm/glm.hpp>
#include <string>

namespace events::volumetric
{

    struct BakeVolumetricNavCommand : ICommand<>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "BakeVolumetricNav"; }
    };

    struct ClearVolumetricNavCommand : ICommand<>
    {
        std::string_view getName() const override { return "ClearVolumetricNav"; }
    };

    struct AddVolumetricAgentCommand : ICommand<>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "AddVolumetricAgent"; }
    };

    struct RemoveVolumetricAgentCommand : ICommand<>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "RemoveVolumetricAgent"; }
    };

    struct SetVolumetricAgentDestinationCommand : ICommand<>
    {
        services::EntityHandle entity;
        glm::vec3 target;
        std::string_view getName() const override { return "SetVolumetricAgentDestination"; }
    };

    struct StopVolumetricAgentCommand : ICommand<>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "StopVolumetricAgent"; }
    };

    struct FindPath3DQuery : IQuery<::volumetric::VolumePath>
    {
        glm::vec3 start;
        glm::vec3 end;
        std::string_view getName() const override { return "FindPath3D"; }
    };

    struct IsPointNavigable3DQuery : IQuery<bool>
    {
        glm::vec3 point;
        std::string_view getName() const override { return "IsPointNavigable3D"; }
    };

    struct GetVolumetricBakeProgressQuery : IQuery<::volumetric::VolumetricBakeProgress>
    {
        std::string_view getName() const override { return "GetVolumetricBakeProgress"; }
    };

    struct VolumetricBakeCompleteNotification : INotification
    {
        bool success = false;
        std::string message;
        std::string_view getName() const override { return "VolumetricBakeComplete"; }
    };

    struct VolumetricAgentReachedDestinationNotification : INotification
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "VolumetricAgentReachedDestination"; }
    };
}
