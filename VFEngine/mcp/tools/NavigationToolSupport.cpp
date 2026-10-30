#include "NavigationToolSupport.hpp"
#include "EnvironmentToolSupport.hpp"
#include "TerrainToolSupport.hpp"
#include "ToolHelpers.hpp"

#include "events/EventDispatcher.hpp"
#include "events/navmesh/NavmeshAuthoringEvents.hpp"
#include "events/navmesh/NavmeshEvents.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "math/TransformUtils.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace mcp::tools::navtool
{
    namespace
    {
        // FollowRoute's marker snap (BehaviorTreeAdapter kRouteSnapTolerance / kRouteSnapRadii).
        constexpr float routeSnapTolerance = 1.0f;
        constexpr std::array<float, 3> routeSnapRadii{5.0f, 25.0f, 100.0f};

        // scene::composeWorldMatrix's walk limit: a corrupt hierarchy with a cycle still ends.
        constexpr int maxHierarchyDepth = 256;

        std::string idText(const services::EntityHandle& entity)
        {
            return std::to_string(toId(entity));
        }
    }

    // ------------------------------------------------------------------
    // Tables
    // ------------------------------------------------------------------

    // The Navigation window's sliders (NavmeshWindow drawAgentSection / drawRegionSection).
    const env::SectionTable<types::NavmeshBakeSettings>& bakeTable()
    {
        using S = types::NavmeshBakeSettings;
        static const env::SectionTable<S> table{
            "bake settings",
            {
                env::floatField<S, &S::agentRadius>("agentRadius", 0.1, 5.0,
                    "Metres the walkable area keeps from walls: at least the largest NavmeshAgent radius."),
                env::floatField<S, &S::agentHeight>("agentHeight", 0.5, 10.0,
                    "Metres of headroom an agent needs: at least the tallest NavmeshAgent height."),
                env::floatField<S, &S::agentMaxSlope>("maxSlope", 0.0, 90.0, "Steepest walkable ground, degrees."),
                env::floatField<S, &S::agentMaxClimb>("stepHeight", 0.0, 5.0,
                    "Highest step an agent climbs, metres (rounded down to whole cellHeight cells)."),
                env::floatField<S, &S::cellSize>("cellSize", 0.05, 2.0,
                    "Voxel width, metres: smaller is more precise and slower (about agentRadius / 2)."),
                env::floatField<S, &S::cellHeight>("cellHeight", 0.05, 2.0, "Voxel height, metres."),
            },
            nullptr
        };
        return table;
    }

    const env::SectionTable<types::NavmeshBakeSettings>& includeTable()
    {
        using S = types::NavmeshBakeSettings;
        static const env::SectionTable<S> table{
            "include",
            {
                env::boolField<S, &S::includeTerrain>("terrain", "Terrain tiles."),
                env::boolField<S, &S::includeStaticMeshes>("staticMeshes",
                    "Every Mesh entity that is not an agent, a Controller or a Dynamic rigid body."),
                env::boolField<S, &S::includeColliders>("colliders",
                    "Box / ConvexMesh / TriangleMesh colliders of entities without a Mesh (triggers never count)."),
                env::boolField<S, &S::includeFoliage>("foliage", "Nav-contributing foliage on loaded terrain tiles."),
            },
            nullptr
        };
        return table;
    }

    // The inspector's ranges (NavmeshAgentDrawer).
    const env::SectionTable<services::NavmeshAgentData>& agentTable()
    {
        using D = services::NavmeshAgentData;
        static const env::SectionTable<D> table{
            "NavmeshAgent",
            {
                env::floatField<D, &D::radius>("radius", 0.1, 5.0, "Metres; at most the navmesh_bake agentRadius."),
                env::floatField<D, &D::height>("height", 0.5, 10.0, "Metres; at most the navmesh_bake agentHeight."),
                env::floatField<D, &D::baseOffset>("baseOffset", -5.0, 5.0,
                    "Height of the pivot above the navmesh surface, metres: 1 for a 2 m capsule centred on its pivot, "
                    "0 for a pivot at the feet."),
                env::floatField<D, &D::maxSpeed>("maxSpeed", 0.0, 50.0, "Metres per second."),
                env::floatField<D, &D::maxAcceleration>("maxAcceleration", 0.0, 100.0, "Metres per second squared."),
                env::floatField<D, &D::turnSpeed>("turnSpeed", 0.0, 1440.0,
                    "Degrees per second toward the travel direction; 0 = at once."),
                env::floatField<D, &D::stoppingDistance>("stoppingDistance", 0.0, 5.0,
                    "Metres from its destination at which the agent stops."),
                env::intField<D, &D::avoidanceQuality>("avoidanceQuality", 0.0, 3.0,
                    "Crowd avoidance preset: 3 = best (and costliest)."),
                env::boolField<D, &D::rootMotionDriven>("rootMotionDriven",
                    "The animation's root motion sets the ground speed (needs an Animator with applyRootMotion)."),
                env::floatField<D, &D::rootMotionSpeedScale>("rootMotionSpeedScale", 0.0, 8.0,
                    "Multiplier on the root-motion speed (still capped by maxSpeed)."),
            },
            nullptr
        };
        return table;
    }

    // ------------------------------------------------------------------
    // Reports
    // ------------------------------------------------------------------

    nlohmann::json bakeSettingsJson(const types::NavmeshBakeSettings& settings)
    {
        nlohmann::json out = env::toJson(bakeTable(), settings);
        out["include"] = env::toJson(includeTable(), settings);
        out["tileSize"] = settings.tileSize;
        out["tileWorldSize"] = env::floatJson(static_cast<float>(settings.tileSize) * settings.cellSize);
        return out;
    }

    nlohmann::json statsJson(const navigation::NavmeshStats& stats)
    {
        nlohmann::json bounds = nullptr;
        if (stats.tileCount > 0)
        {
            bounds = {{"min", pointJson(stats.boundsMin)}, {"max", pointJson(stats.boundsMax)}};
        }
        return {
            {"tiles", stats.tileCount},
            {"polygons", stats.polyCount},
            {"vertices", stats.vertexCount},
            {"bounds", std::move(bounds)}
        };
    }

    nlohmann::json pointJson(const glm::vec3& point)
    {
        return env::vec3Json(point);
    }

    float pathLength(const std::vector<glm::vec3>& points)
    {
        float length = 0.0f;
        for (std::size_t i = 1; i < points.size(); ++i)
        {
            length += glm::distance(points[i - 1], points[i]);
        }
        return length;
    }

    std::string numberText(float value)
    {
        // JSON spells a whole double "2.0"; a message reads "2".
        std::string text = env::floatJson(value).dump();
        if (text.size() > 2 && text.ends_with(".0"))
        {
            text.resize(text.size() - 2);
        }
        return text;
    }

    // ------------------------------------------------------------------
    // Arguments
    // ------------------------------------------------------------------

    glm::vec3 requirePoint(const nlohmann::json& value, std::string_view path)
    {
        return env::requireVec3(value, -maxCoordinate, maxCoordinate, path);
    }

    std::vector<glm::vec3> requirePoints(const nlohmann::json& value, std::size_t minCount, std::size_t maxCount,
                                         std::string_view path)
    {
        if (!value.is_array())
        {
            throw ArgError("'" + std::string(path) + "' must be an array of [x, y, z] points (got " +
                           env::detail::valueText(value) + ")");
        }
        if (value.size() < minCount || value.size() > maxCount)
        {
            throw ArgError("'" + std::string(path) + "' must hold " + std::to_string(minCount) + "-" +
                           std::to_string(maxCount) + " points (got " + std::to_string(value.size()) + ")");
        }
        std::vector<glm::vec3> points;
        points.reserve(value.size());
        for (std::size_t i = 0; i < value.size(); ++i)
        {
            points.push_back(requirePoint(value[i], std::string(path) + "[" + std::to_string(i) + "]"));
        }
        return points;
    }

    Endpoint requireEndpoint(const nlohmann::json& value, std::string_view path)
    {
        Endpoint endpoint;
        if (value.is_array())
        {
            endpoint.point = requirePoint(value, path);
            return endpoint;
        }

        // An entity id; models frequently send 3.0 for 3.
        constexpr int64_t maxId = static_cast<int64_t>(std::numeric_limits<uint32_t>::max());
        int64_t id = -1;
        if (value.is_number_unsigned())
        {
            id = value.get<uint64_t>() <= static_cast<uint64_t>(maxId) ? static_cast<int64_t>(value.get<uint64_t>()) : -1;
        }
        else if (value.is_number_integer())
        {
            id = value.get<int64_t>();
        }
        else if (value.is_number_float() && std::isfinite(value.get<double>()) &&
                 value.get<double>() == std::trunc(value.get<double>()))
        {
            const double number = value.get<double>();
            id = number >= 0.0 && number <= static_cast<double>(maxId) ? static_cast<int64_t>(number) : -1;
        }
        else
        {
            throw ArgError("'" + std::string(path) + "' must be [x, y, z] or an entity id (got " +
                           env::detail::valueText(value) + ")");
        }
        if (id < 0 || id > maxId)
        {
            throw ArgError("'" + std::string(path) + "' is not a valid entity id (got " + env::detail::valueText(value) +
                           ")");
        }

        std::optional<services::EntityData> entity = findEntity(toHandle(static_cast<uint32_t>(id)));
        if (!entity.has_value())
        {
            throw std::runtime_error("Entity " + std::to_string(id) + " (argument '" + std::string(path) +
                                     "') does not exist; use entity_find or scene_get_hierarchy");
        }
        endpoint.point = worldPosition(*entity);
        if (entity->hasComponent(services::ComponentTypeId::NavmeshAgent))
        {
            // The crowd walks the surface point below the pivot.
            try
            {
                events::scene::GetNavmeshAgentDataQuery query;
                query.entity = entity->handle;
                if (const std::optional<services::NavmeshAgentData> data = events::EventDispatcher::instance().query(query))
                {
                    endpoint.baseOffset = data->baseOffset;
                    endpoint.point.y -= data->baseOffset;
                }
            }
            catch (const std::runtime_error&)
            {
                // No navmesh service: the world position as it is.
            }
        }
        endpoint.entity = std::move(entity);
        return endpoint;
    }

    // ------------------------------------------------------------------
    // World positions
    // ------------------------------------------------------------------

    glm::mat4 localMatrix(const services::TransformData& transform)
    {
        return math::composeMatrix(transform.position, transform.rotation, transform.scale);
    }

    glm::mat4 worldMatrix(const services::EntityData& entity)
    {
        glm::mat4 world = localMatrix(entity.localTransform);
        std::optional<services::EntityHandle> parent = entity.parent;
        for (int depth = 1; depth < maxHierarchyDepth && parent.has_value() && parent->isValid(); ++depth)
        {
            const std::optional<services::EntityData> data = findEntity(*parent);
            if (!data.has_value())
            {
                break;
            }
            world = localMatrix(data->localTransform) * world;
            parent = data->parent;
        }
        return world;
    }

    glm::vec3 worldPosition(const services::EntityData& entity)
    {
        return glm::vec3(worldMatrix(entity)[3]);
    }

    // ------------------------------------------------------------------
    // Navmesh probes
    // ------------------------------------------------------------------

    bool hasNavmesh()
    {
        try
        {
            return events::EventDispatcher::instance().query(events::navmesh::HasNavmeshQuery{});
        }
        catch (const std::runtime_error&)
        {
            return false;
        }
    }

    Probe probe(const glm::vec3& point, float snapRadius)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        Probe result;

        events::navmesh::IsPointOnNavmeshQuery onMesh;
        onMesh.point = point;
        onMesh.tolerance = onNavmeshTolerance;
        result.onNavmesh = dispatcher.query(onMesh);

        events::navmesh::GetClosestPointQuery closest;
        closest.point = point;
        closest.searchRadius = snapRadius;
        const glm::vec3 candidate = dispatcher.query(closest);
        if (candidate != point)
        {
            result.nearest = candidate;  // a miss would have echoed the point
        }
        else if (result.onNavmesh)
        {
            result.nearest = point;      // the point lies on the navmesh itself
        }
        return result;
    }

    nlohmann::json probeJson(const Probe& probe, const glm::vec3& point)
    {
        return {
            {"onNavmesh", probe.onNavmesh},
            {"nearest", probe.nearest.has_value() ? pointJson(*probe.nearest) : nlohmann::json(nullptr)},
            {"offset", probe.nearest.has_value() ? env::floatJson(glm::distance(point, *probe.nearest))
                                                 : nlohmann::json(nullptr)}
        };
    }

    std::optional<glm::vec3> snapToNavmesh(const glm::vec3& point)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::navmesh::IsPointOnNavmeshQuery onMesh;
        onMesh.point = point;
        onMesh.tolerance = routeSnapTolerance;
        if (dispatcher.query(onMesh))
        {
            return point;
        }

        // GetClosestPoint echoes its input when nothing is found, so every candidate is checked again.
        for (const float radius : routeSnapRadii)
        {
            events::navmesh::GetClosestPointQuery closest;
            closest.point = point;
            closest.searchRadius = radius;
            const glm::vec3 candidate = dispatcher.query(closest);

            events::navmesh::IsPointOnNavmeshQuery check;
            check.point = candidate;
            check.tolerance = routeSnapTolerance;
            if (dispatcher.query(check))
            {
                return candidate;
            }
        }
        return std::nullopt;
    }

    // ------------------------------------------------------------------
    // Scene
    // ------------------------------------------------------------------

    std::vector<services::EntityData> entitiesWith(services::ComponentTypeId type)
    {
        const services::SceneHierarchyData hierarchy =
            events::EventDispatcher::instance().query(events::scene::GetSceneHierarchyQuery{});
        std::vector<services::EntityData> out;
        for (const services::EntityData& entity : hierarchy.entities)
        {
            if (entity.hasComponent(type))
            {
                out.push_back(entity);
            }
        }
        return out;
    }

    services::EntityData requireRoute(const ArgReader& reader, const char* name)
    {
        services::EntityData route = requireEntity(reader, name);
        if (route.hasComponent(services::ComponentTypeId::NavRoute))
        {
            return route;
        }
        std::string message = "Entity " + idText(route.handle) + " ('" + route.name +
                              "') is not a route (it has no NavRoute component)";
        if (route.parent.has_value())
        {
            const std::optional<services::EntityData> parent = findEntity(*route.parent);
            if (parent.has_value() && parent->hasComponent(services::ComponentTypeId::NavRoute))
            {
                message += ": it is a marker of route " + idText(parent->handle) + " ('" + parent->name +
                           "'), pass that instead";
            }
        }
        throw std::runtime_error(message + "; route_get lists the routes");
    }

    std::vector<services::EntityData> routeMarkers(const services::EntityData& route)
    {
        std::vector<services::EntityData> markers;
        markers.reserve(route.children.size());
        for (const services::EntityHandle& child : route.children)
        {
            if (std::optional<services::EntityData> marker = findEntity(child))
            {
                markers.push_back(std::move(*marker));
            }
        }
        return markers;
    }

    std::vector<std::string> agentWarnings(const services::EntityData& entity, const services::NavmeshAgentData& data)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        std::vector<std::string> warnings;

        // NavmeshAgentManager spawns the crowd agent at TransformComponent::position and writes the crowd's
        // position back into it.
        if (entity.parent.has_value() && entity.parent->isValid() && *entity.parent != sceneRoot())
        {
            warnings.push_back("Parented: the crowd reads and writes the agent's LOCAL position as a world position, "
                               "so under a moved, rotated or scaled parent it is drawn away from where it walks. Keep "
                               "NavmeshAgent entities at the scene root (entity_set_parent without 'parent').");
        }

        // The animator writes root motion only with applyRootMotion; without it the crowd falls back to maxSpeed.
        if (data.rootMotionDriven)
        {
            bool rootMotion = true;  // unknown without a mesh service: no warning
            try
            {
                events::scene::GetMeshDataQuery query;
                query.entity = entity.handle;
                const std::optional<services::MeshData> mesh = dispatcher.query(query);
                rootMotion = mesh.has_value() && mesh->applyRootMotion && mesh->animatorRef.isValid();
            }
            catch (const std::runtime_error&)
            {
            }
            if (!rootMotion)
            {
                warnings.push_back("rootMotionDriven is on, but the entity plays no root motion (that needs a Mesh "
                                   "with an Animator and applyRootMotion on), so it moves at maxSpeed. Turn "
                                   "applyRootMotion on (component_set Animator {applyRootMotion: true}) or "
                                   "rootMotionDriven off.");
            }
        }

        events::navmesh::NavmeshStatus status;
        try
        {
            status = dispatcher.query(events::navmesh::GetNavmeshStatusQuery{});
        }
        catch (const std::runtime_error&)
        {
            return warnings;  // no navmesh service
        }
        if (!status.hasNavmesh)
        {
            warnings.push_back("No navmesh is baked: the agent cannot move until navmesh_bake builds one.");
            return warnings;
        }

        if (data.radius > status.settings.agentRadius + sizeTolerance)
        {
            warnings.push_back("radius " + numberText(data.radius) + " is above the navmesh's agentRadius " +
                               numberText(status.settings.agentRadius) +
                               ": the agent can clip walls and corners and stick in passages baked for a thinner "
                               "agent. Bake again with navmesh_bake {agentRadius: " + numberText(data.radius) +
                               "} or lower its radius.");
        }
        if (data.height > status.settings.agentHeight + sizeTolerance)
        {
            warnings.push_back("height " + numberText(data.height) + " is above the navmesh's agentHeight " +
                               numberText(status.settings.agentHeight) +
                               ": the navmesh lets it walk under ceilings lower than itself. Bake again with "
                               "navmesh_bake {agentHeight: " + numberText(data.height) + "} or lower its height.");
        }

        try
        {
            const glm::vec3 feet = worldPosition(entity) - glm::vec3(0.0f, data.baseOffset, 0.0f);
            const Probe found = probe(feet, agentProbeRadius);
            if (!found.onNavmesh)
            {
                if (found.nearest.has_value())
                {
                    warnings.push_back("Off the navmesh: its feet (world position minus baseOffset) are " +
                                       numberText(glm::distance(feet, *found.nearest)) +
                                       " m from the nearest navmesh point " + pointJson(*found.nearest).dump() +
                                       ". Move it onto the navmesh, or fix baseOffset (the pivot's height above the "
                                       "ground: about 1 for a 2 m capsule centred on its pivot, 0 for a pivot at the "
                                       "feet).");
                }
                else
                {
                    warnings.push_back("Off the navmesh: nothing walkable lies within " + numberText(agentProbeRadius) +
                                       " m of its feet (world position minus baseOffset), so it cannot join the "
                                       "crowd. Move it onto the navmesh, or bake one that covers it.");
                }
            }
        }
        catch (const std::runtime_error&)
        {
        }
        return warnings;
    }

    std::optional<std::string> editBlocker(std::string_view action, std::string_view worldModeReason)
    {
        if (std::optional<std::string> blocked = env::editBlocker(action))
        {
            return blocked;
        }
        if (isWorldModeActive())
        {
            return "Cannot " + std::string(action) + " in World mode: " + std::string(worldModeReason);
        }
        return std::nullopt;
    }

    void deleteQuietly(const services::EntityHandle& entity)
    {
        try
        {
            events::scene::DeleteEntityCommand command;
            command.entity = entity;
            events::EventDispatcher::instance().execute(command);
        }
        catch (const std::exception&)
        {
            // Cleanup of a failed creation: the original error is what the caller reports.
        }
    }
}
