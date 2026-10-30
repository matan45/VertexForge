#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/VolumetricNavData.hpp"
#include "navigation/volumetric/VolumetricPathfinder.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Volumetric navigation for flying agents (rewritten for VK-1660). Each VolumetricNavVolume entity bakes its own
// voxel grid in the background (refused in Play), saved to a .vfVolNav that the component references (bakedRef)
// and that reloads with the scene. VolumetricAgent entities path through the loaded volumes during Play.
// Handled by services::VolumetricNavServiceImpl. MAIN THREAD unless stated.
namespace events::volumetric
{
    // === Bake ===

    // Snapshots the volume's world geometry and voxelizes it in the background; poll the ticket. Refused (never
    // queued): in Play, an entity without a VolumetricNavVolume, a bake of that volume already running, an invalid
    // box or settings, more voxels than ::volumetric::kMaxVoxels. A finished bake replaces the volume's loaded
    // grid; nothing is written to disk until SaveVolumetricVolumeCommand.
    struct RequestVolumetricBakeCommand : ICommand<services::VolumetricBakeTicket>
    {
        services::EntityHandle volume;
        std::string_view getName() const override { return "RequestVolumetricBake"; }
    };

    struct GetVolumetricBakeOutcomeQuery : IQuery<services::VolumetricBakeOutcome>
    {
        uint64_t ticket = 0;
        std::string_view getName() const override { return "GetVolumetricBakeOutcome"; }
    };

    // Cancels the volume's running bake (its outcome becomes Cancelled); an invalid handle cancels every bake.
    struct CancelVolumetricBakeCommand : ICommand<>
    {
        services::EntityHandle volume;
        std::string_view getName() const override { return "CancelVolumetricBake"; }
    };

    // Writes the volume's loaded grid atomically to <directory>/<volume name>_<uuid>.vfVolNav, points the
    // component's bakedRef at it and publishes AssetSavedNotification (save the scene to keep the reference).
    // directory "" = the folder of the current bakedRef, else <project>/assets/volnav/<scene file stem>.
    // Refused when the volume has no loaded grid.
    struct SaveVolumetricVolumeCommand : ICommand<services::VolumetricSaveResult>
    {
        services::EntityHandle volume;
        std::string directory;
        std::string_view getName() const override { return "SaveVolumetricVolume"; }
    };

    // Loads the grid of every VolumetricNavVolume that has a bakedRef (ScenePersistenceService::finishLoad); a
    // missing or undecodable file logs one warning. Returns how many were loaded.
    struct LoadSceneVolumetricVolumesCommand : ICommand<int>
    {
        std::string_view getName() const override { return "LoadSceneVolumetricVolumes"; }
    };

    // Drops a volume's loaded grid and clears its bakedRef (the file stays on disk); an invalid handle = every
    // volume.
    struct ClearVolumetricNavCommand : ICommand<>
    {
        services::EntityHandle volume;
        std::string_view getName() const override { return "ClearVolumetricNav"; }
    };

    // A scene change (new scene / disk load): cancels the bakes and drops every loaded grid and agent. Components
    // are not touched.
    struct UnloadVolumetricNavCommand : ICommand<>
    {
        std::string_view getName() const override { return "UnloadVolumetricNav"; }
    };

    // nullopt when the entity has no VolumetricNavVolume.
    struct GetVolumetricVolumeStatusQuery : IQuery<std::optional<services::VolumetricVolumeStatus>>
    {
        services::EntityHandle volume;
        std::string_view getName() const override { return "GetVolumetricVolumeStatus"; }
    };

    // Every VolumetricNavVolume of the scene.
    struct ListVolumetricVolumesQuery : IQuery<std::vector<services::VolumetricVolumeStatus>>
    {
        std::string_view getName() const override { return "ListVolumetricVolumes"; }
    };

    // === Queries (any thread) ===

    struct FindPath3DQuery : IQuery<::volumetric::VolumePathResult>
    {
        glm::vec3 start{0.0f};
        glm::vec3 end{0.0f};
        float snapRadius = 2.0f;
        std::string_view getName() const override { return "FindPath3D"; }
    };

    // The point's voxel is navigable in a loaded volume.
    struct IsPointNavigable3DQuery : IQuery<bool>
    {
        glm::vec3 point{0.0f};
        std::string_view getName() const override { return "IsPointNavigable3D"; }
    };

    // The point when its voxel is navigable, else the nearest navigable voxel center within snapRadius; nullopt
    // when there is none (or no volume holds the point).
    struct GetNearestNavigable3DQuery : IQuery<std::optional<glm::vec3>>
    {
        glm::vec3 point{0.0f};
        float snapRadius = 2.0f;
        std::string_view getName() const override { return "GetNearestNavigable3D"; }
    };

    // === Agents (Play; the behavior-tree task may send these) ===

    // Paths the agent to `target`, registering it with the agent manager on first use. Ignored outside Play. The
    // agent's state becomes Moving, then Arrived or Failed.
    struct SetVolumetricAgentDestinationCommand : ICommand<>
    {
        services::EntityHandle entity;
        glm::vec3 target{0.0f};
        std::string_view getName() const override { return "SetVolumetricAgentDestination"; }
    };

    // The agent stops where it is (state Idle).
    struct StopVolumetricAgentCommand : ICommand<>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "StopVolumetricAgent"; }
    };

    // nullopt when the entity has no VolumetricAgent; Idle until it is given a destination.
    struct GetVolumetricAgentStateQuery : IQuery<std::optional<services::VolumetricAgentState>>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetVolumetricAgentState"; }
    };

    // === Component data ===

    // nullopt when the entity has no VolumetricNavVolume.
    struct GetVolumetricNavVolumeDataQuery : IQuery<std::optional<services::VolumetricNavVolumeData>>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetVolumetricNavVolumeData"; }
    };

    // Writes the editable fields (the read-only ones are ignored). False for a missing component or a value the
    // bake cannot use: non-finite, halfExtents <= 0, voxelSize <= 0, connectivity other than 6 / 26, a negative
    // clearance. A loaded grid that no longer matches reads as stale until it is baked again.
    struct SetVolumetricNavVolumeDataCommand : ICommand<bool>
    {
        services::EntityHandle entity;
        services::VolumetricNavVolumeData data;
        std::string_view getName() const override { return "SetVolumetricNavVolumeData"; }
    };

    // nullopt when the entity has no VolumetricAgent.
    struct GetVolumetricAgentDataQuery : IQuery<std::optional<services::VolumetricAgentData>>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetVolumetricAgentData"; }
    };

    // Writes the editable fields. False for a missing component or a non-finite value, radius <= 0, or a negative
    // speed, acceleration, stopping distance or turn speed. A registered agent uses them from its next update.
    struct SetVolumetricAgentDataCommand : ICommand<bool>
    {
        services::EntityHandle entity;
        services::VolumetricAgentData data;
        std::string_view getName() const override { return "SetVolumetricAgentData"; }
    };

    // === Notifications ===

    struct VolumetricBakeCompleteNotification : INotification
    {
        services::EntityHandle volume;
        uint64_t ticket = 0;
        bool success = false;
        std::string message;
        std::string_view getName() const override { return "VolumetricBakeComplete"; }
    };

    struct VolumetricAgentReachedDestinationNotification : INotification
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "VolumetricAgentReachedDestination"; }
    };

    struct VolumetricAgentPathFailedNotification : INotification
    {
        services::EntityHandle entity;
        std::string reason; // VolumetricAgentState::failReason
        std::string_view getName() const override { return "VolumetricAgentPathFailed"; }
    };
}
