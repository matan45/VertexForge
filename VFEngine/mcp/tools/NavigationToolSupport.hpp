#pragma once

#include "CoreTools.hpp"
#include "SettingsTable.hpp"
#include "../protocol/ArgReader.hpp"

#include "data/DTOs.hpp"
#include "data/EntityHandle.hpp"
#include "navigation/NavmeshData.hpp"
#include "types/NavmeshTypes.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// VK-1660: helpers shared by the navigation tools (NavigationTools.cpp: navmesh_*; RouteTools.cpp: route_*) and the
// NavmeshAgent binding (NavmeshAgentBinding.cpp): the setting tables, point arguments, fresh world positions,
// navmesh probes, route lookup and the agent warnings. Functions that dispatch run on the main thread; the others
// say "Any thread".
namespace mcp::tools::navtool
{
    // ------------------------------------------------------------------
    // Tables (any thread)
    // ------------------------------------------------------------------

    // navmesh_bake's settings over types::NavmeshBakeSettings, with the Navigation window's ranges: agentRadius,
    // agentHeight, maxSlope (agentMaxSlope), stepHeight (agentMaxClimb), cellSize, cellHeight.
    const env::SectionTable<types::NavmeshBakeSettings>& bakeTable();

    // navmesh_bake include {terrain, staticMeshes, colliders, foliage}: the include* flags.
    const env::SectionTable<types::NavmeshBakeSettings>& includeTable();

    // The NavmeshAgent binding's writable fields over services::NavmeshAgentData, with the inspector's ranges
    // (NavmeshAgentDrawer). The read-only inCrowd and warnings are reported beside them.
    const env::SectionTable<services::NavmeshAgentData>& agentTable();

    // ------------------------------------------------------------------
    // Reports (any thread)
    // ------------------------------------------------------------------

    // The bake table's keys, include {...}, tileSize (cells) and tileWorldSize (metres).
    nlohmann::json bakeSettingsJson(const types::NavmeshBakeSettings& settings);

    // {tiles, polygons, vertices, bounds {min, max}} (bounds null without tiles).
    nlohmann::json statsJson(const navigation::NavmeshStats& stats);

    // [x, y, z], each the shortest decimal that round-trips the float.
    nlohmann::json pointJson(const glm::vec3& point);

    // The sum of the segment lengths.
    float pathLength(const std::vector<glm::vec3>& points);

    // A float in its shortest round-trip spelling for messages, a whole number without ".0" ("2", "0.25").
    std::string numberText(float value);

    // ------------------------------------------------------------------
    // Arguments
    // ------------------------------------------------------------------

    // Largest accepted |coordinate| of a point argument, in metres.
    inline constexpr double maxCoordinate = 1.0e6;

    // [x, y, z] with every coordinate finite and within +/-maxCoordinate; ArgError naming `path`. Any thread.
    glm::vec3 requirePoint(const nlohmann::json& value, std::string_view path);

    // An array of minCount..maxCount points; element errors name 'path[i]'. Any thread.
    std::vector<glm::vec3> requirePoints(const nlohmann::json& value, std::size_t minCount, std::size_t maxCount,
                                         std::string_view path);

    // A navmesh_find_path endpoint: [x, y, z], or an entity id standing for its world position (for a NavmeshAgent,
    // its feet: the world position lowered by its baseOffset, the surface point the crowd walks).
    struct Endpoint
    {
        glm::vec3 point{0.0f};
        std::optional<services::EntityData> entity;
        float baseOffset = 0.0f;  // subtracted from a NavmeshAgent's world position
    };

    // ArgError for a malformed value; std::runtime_error for an entity id that names nothing.
    Endpoint requireEndpoint(const nlohmann::json& value, std::string_view path);

    // ------------------------------------------------------------------
    // World positions
    // ------------------------------------------------------------------

    // The local transform as a matrix (TransformComponent::getMatrix order). Any thread.
    glm::mat4 localMatrix(const services::TransformData& transform);

    // The entity's world matrix composed from the LOCAL transforms up its parent chain (GetEntityQuery), as
    // scene::composeWorldMatrix and FollowRoute compute it. Unlike EntityData::worldTransform, which the frame's
    // Transforms task updates, it sees edits made earlier in the same call.
    glm::mat4 worldMatrix(const services::EntityData& entity);
    glm::vec3 worldPosition(const services::EntityData& entity);

    // ------------------------------------------------------------------
    // Navmesh probes
    // ------------------------------------------------------------------

    // IsPointOnNavmeshQuery's box half-size for "on the navmesh" (the query's default).
    inline constexpr float onNavmeshTolerance = 0.5f;

    // How far an agent's feet are searched for the navmesh (the binding's warnings, navmesh_get_info).
    inline constexpr float agentProbeRadius = 5.0f;

    // An agent counts as larger than the bake when its radius or height exceeds the bake's by more than this
    // (float noise from a round trip does not).
    inline constexpr float sizeTolerance = 1.0e-4f;

    // HasNavmeshQuery; false when no navmesh service is registered.
    bool hasNavmesh();

    struct Probe
    {
        bool onNavmesh = false;            // IsPointOnNavmeshQuery at onNavmeshTolerance
        std::optional<glm::vec3> nearest;  // the closest navmesh point within the snap radius
    };

    // The point against the navmesh. GetClosestPointQuery returns its input unchanged when nothing lies within the
    // radius, so an unchanged answer counts only when the point itself is on the navmesh.
    Probe probe(const glm::vec3& point, float snapRadius);

    // {onNavmesh, nearest, offset}: nearest and offset (metres from the point) are null when nothing is in reach.
    // Any thread.
    nlohmann::json probeJson(const Probe& probe, const glm::vec3& point);

    // FollowRoute's marker snap (BehaviorTreeAdapter::snapToNavmesh): the point itself when it is within 1 m of the
    // navmesh, else the closest navmesh point within 5, 25, then 100 m; nullopt when there is none.
    std::optional<glm::vec3> snapToNavmesh(const glm::vec3& point);

    // ------------------------------------------------------------------
    // Scene
    // ------------------------------------------------------------------

    // Every entity of the scene holding `type` (GetSceneHierarchyQuery), in hierarchy order.
    std::vector<services::EntityData> entitiesWith(services::ComponentTypeId type);

    // The route the argument names: an entity with a NavRoute component. std::runtime_error otherwise (a marker
    // names its route in the message).
    services::EntityData requireRoute(const ArgReader& reader, const char* name);

    // The route's live markers: its children in hierarchy (ChildrenComponent) order, the order FollowRoute walks.
    std::vector<services::EntityData> routeMarkers(const services::EntityData& route);

    // Why the agent may not move as configured: a parent other than the scene root, a shape larger than the bake,
    // root motion it cannot get, feet off the navmesh, no navmesh at all. A probe whose handler is missing is
    // skipped. The NavmeshAgent binding's read-only warnings, and navmesh_get_info's.
    std::vector<std::string> agentWarnings(const services::EntityData& entity, const services::NavmeshAgentData& data);

    // nullopt when a navmesh or route edit may run now, else why not: Play mode or a pending scene load
    // (env::editBlocker), then World mode. `action` completes "Cannot <action> ...", `worldModeReason` follows
    // "in World mode: ".
    std::optional<std::string> editBlocker(std::string_view action, std::string_view worldModeReason);

    // DeleteEntityCommand, swallowing failures: the cleanup of a half-built creation.
    void deleteQuietly(const services::EntityHandle& entity);

    // ------------------------------------------------------------------
    // Registration of RouteTools.cpp (called by registerNavigationTools in NavigationTools.cpp)
    // ------------------------------------------------------------------

    void registerRouteTools(ToolRegistry& registry, const ToolContext& context);  // route_create, route_set, route_get
}
