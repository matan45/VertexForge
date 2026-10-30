#pragma once
#include "../EventTypes.hpp"
#include "navigation/NavmeshData.hpp"
#include "types/NavmeshTypes.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// VK-1660: a ticketed scene navmesh bake and a status summary for tools (MCP navmesh_*). Handled by
// services::NavmeshServiceImpl. MAIN THREAD ONLY.
namespace events::navmesh
{
    // World-space box a bake is limited to.
    struct NavmeshBakeBounds
    {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
    };

    struct NavmeshBakeTicket
    {
        bool accepted = false;
        uint64_t ticket = 0; // non-zero when accepted
        std::string error;   // why the bake was refused
    };

    // Starts the same asynchronous scene bake as BakeNavmeshCommand and returns a ticket to poll with
    // GetNavmeshBakeOutcomeQuery. Refused - never silently dropped - while another bake runs. `bounds`
    // limits the bake to that world box: the navmesh covers only the tiles it overlaps (each built whole),
    // and on Y the box is a floor and a ceiling for the rasterized geometry. The bake writes nothing to
    // disk; persist a success with SaveNavmeshTiledCommand (which points the scene root's NavmeshComponent
    // at it) and a scene save.
    struct RequestNavmeshBakeCommand : ICommand<NavmeshBakeTicket>
    {
        types::NavmeshBakeSettings settings;
        std::optional<NavmeshBakeBounds> bounds;

        std::string_view getName() const override { return "RequestNavmeshBake"; }
    };

    enum class NavmeshBakeState : uint8_t
    {
        Unknown = 0, // no such ticket
        Running,
        Succeeded,
        Failed
    };

    struct NavmeshBakeOutcome
    {
        uint64_t ticket = 0;
        NavmeshBakeState state = NavmeshBakeState::Unknown;
        float progress = 0.0f;          // 0-1
        std::string stage;              // the bake stage while Running
        std::string message;            // Failed: why
        navigation::NavmeshStats stats; // Succeeded: the navmesh the bake produced
    };

    struct GetNavmeshBakeOutcomeQuery : IQuery<NavmeshBakeOutcome>
    {
        uint64_t ticket = 0;

        std::string_view getName() const override { return "GetNavmeshBakeOutcome"; }
    };

    struct NavmeshStatus
    {
        bool hasNavmesh = false;
        bool bakeRunning = false;
        navigation::NavmeshStats stats;
        types::NavmeshBakeSettings settings; // the current navmesh's bake settings
        std::string indexPath;               // the scene's saved navmesh (root NavmeshComponent); "" = none
        bool unsavedChanges = false;         // a bake finished since the navmesh was last saved or loaded
        bool streamingEnabled = false;
    };

    struct GetNavmeshStatusQuery : IQuery<NavmeshStatus>
    {
        std::string_view getName() const override { return "GetNavmeshStatus"; }
    };
}
