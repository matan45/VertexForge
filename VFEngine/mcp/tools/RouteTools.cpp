#include "CoreTools.hpp"
#include "NavigationToolSupport.hpp"
#include "SettingsTable.hpp"
#include "ToolHelpers.hpp"
#include "../protocol/ArgReader.hpp"
#include "../undo/McpUndo.hpp"
#include "../undo/McpUndoCommands.hpp"

#include "behaviortree/BehaviorTreeTypes.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ai/BehaviorTreeComponentEvents.hpp"
#include "events/ai/BehaviorTreeEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// VK-1660 navigation tools, part 2: route_create, route_set and route_get. A route is a container entity with a
// NavRouteComponent whose ordered children are its markers; the behavior tree's FollowRoute task walks the markers'
// WORLD positions in child order (BehaviorTreeAdapter::gatherRoutePoints). route_create is ONE EntityLifetimeUndo of
// the whole route; route_set is one batch of marker transforms, marker creations / deletions and a rename.
namespace mcp::tools::navtool
{
    namespace
    {
        constexpr std::size_t minPoints = 2;
        constexpr std::size_t maxPoints = 256;

        // A snapped point that moved farther than this on XZ gets a warning (dropping onto the ground does not).
        constexpr float snapWarningDistance = 0.5f;

        // A route transform whose matrix is this close to singular cannot place markers (a zero scale).
        constexpr float minDeterminant = 1.0e-12f;

        constexpr const char* worldModeReason =
            "every new entity there is assigned to a streaming sector by its position, so a route's markers would "
            "stream in and out with their sectors. Build routes in an ordinary scene.";

        std::string idText(const services::EntityHandle& entity)
        {
            return std::to_string(toId(entity));
        }

        std::string markerName(std::size_t index)
        {
            return "WP" + std::to_string(index);
        }

        nlohmann::json pointsSchema(std::string description)
        {
            nlohmann::json points = schema::array(schema::vec3("World [x, y, z]"), std::move(description));
            points["minItems"] = minPoints;
            points["maxItems"] = maxPoints;
            return points;
        }

        // The points snapped as FollowRoute snaps the markers, with a warning for each one that moved sideways or
        // found no navmesh.
        std::vector<glm::vec3> snapPoints(const std::vector<glm::vec3>& points, std::vector<std::string>& warnings)
        {
            if (!hasNavmesh())
            {
                warnings.push_back("No navmesh is baked, so the points were not snapped: run navmesh_bake, then "
                                   "route_set {route, snapToNavmesh: true}.");
                return points;
            }
            std::vector<glm::vec3> snapped;
            snapped.reserve(points.size());
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                const std::optional<glm::vec3> onMesh = snapToNavmesh(points[i]);
                if (!onMesh.has_value())
                {
                    warnings.push_back("Point " + std::to_string(i) + " " + pointJson(points[i]).dump() +
                                       " is more than 100 m from the navmesh: kept as given, and an agent cannot walk "
                                       "to it.");
                    snapped.push_back(points[i]);
                    continue;
                }
                const float sideways = glm::length(glm::vec2(onMesh->x - points[i].x, onMesh->z - points[i].z));
                if (sideways > snapWarningDistance)
                {
                    warnings.push_back("Point " + std::to_string(i) + " moved " + numberText(sideways) +
                                       " m sideways onto the navmesh, to " + pointJson(*onMesh).dump() + ".");
                }
                snapped.push_back(*onMesh);
            }
            return snapped;
        }

        // The inverse of the route's world matrix, which turns a marker's world position into its local one.
        glm::mat4 worldToRoute(const services::EntityData& route)
        {
            const glm::mat4 world = worldMatrix(route);
            if (world == glm::mat4(1.0f))
            {
                return world;
            }
            const float determinant = glm::determinant(world);
            if (!std::isfinite(determinant) || std::fabs(determinant) <= minDeterminant)
            {
                throw std::runtime_error("Route " + idText(route.handle) + " ('" + route.name + "') has a degenerate "
                                         "transform (a zero scale on it or a parent), so markers cannot be placed in "
                                         "it; reset it with entity_set_transform");
            }
            return glm::inverse(world);
        }

        glm::vec3 localPoint(const glm::mat4& toLocal, const glm::vec3& world)
        {
            return toLocal == glm::mat4(1.0f) ? world : glm::vec3(toLocal * glm::vec4(world, 1.0f));
        }

        // A marker named WP<index> under the route at the local position (identity rotation and scale).
        services::EntityHandle createMarker(const services::EntityHandle& route, std::size_t index, const glm::vec3& local)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::scene::CreateEntityCommand create;
            create.name = markerName(index);
            create.parent = route;
            const services::EntityHandle marker = dispatcher.execute(create);
            if (!marker.isValid())
            {
                throw std::runtime_error("creating route marker " + create.name + " failed");
            }
            events::scene::SetTransformCommand place;
            place.entity = marker;
            place.transform.position = local;
            dispatcher.execute(place);
            return marker;
        }

        // The markers' world positions (the route's world matrix composed once) and ids, in FollowRoute order.
        struct RouteView
        {
            nlohmann::json points = nlohmann::json::array();
            nlohmann::json markerIds = nlohmann::json::array();
            std::size_t markers = 0;
        };

        RouteView viewOf(const services::EntityData& route)
        {
            const glm::mat4 world = worldMatrix(route);
            RouteView view;
            for (const services::EntityData& marker : routeMarkers(route))
            {
                view.points.push_back(pointJson(glm::vec3((world * localMatrix(marker.localTransform))[3])));
                view.markerIds.push_back(toId(marker.handle));
                ++view.markers;
            }
            return view;
        }

        std::vector<std::string> routeWarnings(const services::EntityData& route, std::size_t markers)
        {
            std::vector<std::string> warnings;
            if (markers == 0)
            {
                warnings.push_back("The route has no markers, so FollowRoute fails on it: add them with route_set "
                                   "{route, points}.");
            }
            if (worldMatrix(route) != glm::mat4(1.0f))
            {
                warnings.push_back("The route entity is not at the origin with an identity transform: the behavior "
                                   "tree's FollowRoute walks the markers' WORLD positions (the points reported here), "
                                   "but a script that reads the markers' LOCAL positions sees other points.");
            }
            return warnings;
        }

        // The keys a FollowRoute node of the tree reads its route from (catalogue default "route").
        std::unordered_set<std::string> followRouteKeys(const behaviortree::BehaviorTreeData* tree)
        {
            std::unordered_set<std::string> keys;
            if (tree == nullptr)
            {
                return keys;
            }
            for (const behaviortree::BTNode& node : tree->graph.nodes)
            {
                if (node.type == behaviortree::BTNodeType::FollowRoute)
                {
                    keys.insert(behaviortree::getNodeProperty<std::string>(node, "routeKey", std::string("route")));
                }
            }
            return keys;
        }

        // Who follows each route (route id -> [{entity, name, key, source}]): the BehaviorTree components whose
        // start values hold the route as an Entity value, and in Play the live blackboards holding it as an Entity
        // value or in the Int key a FollowRoute node reads. Scripts that find a route by name are not visible here.
        std::unordered_map<uint64_t, nlohmann::json> routeFollowers(const std::vector<services::EntityData>& routes)
        {
            std::unordered_map<uint64_t, nlohmann::json> followers;
            for (const services::EntityData& route : routes)
            {
                followers[route.handle.id] = nlohmann::json::array();
            }
            if (routes.empty())
            {
                return followers;
            }

            auto& dispatcher = events::EventDispatcher::instance();
            const auto add = [&followers](uint64_t route, const services::EntityData& owner, const std::string& key,
                                          const char* source)
            {
                const auto it = followers.find(route);
                if (it != followers.end())
                {
                    it->second.push_back(nlohmann::json{
                        {"entity", toId(owner.handle)}, {"name", owner.name}, {"key", key}, {"source", source}});
                }
            };

            for (const services::EntityData& owner : entitiesWith(services::ComponentTypeId::BehaviorTree))
            {
                try
                {
                    events::ai::GetBehaviorTreeComponentDataQuery query;
                    query.entity = owner.handle;
                    if (const std::optional<services::BehaviorTreeComponentData> data = dispatcher.query(query))
                    {
                        for (const services::BehaviorTreeInitialValueData& value : data->initialValues)
                        {
                            if (const auto* target = std::get_if<services::EntityHandle>(&value.value))
                            {
                                add(target->id, owner, value.key, "startValue");
                            }
                        }
                    }
                }
                catch (const std::runtime_error&)
                {
                    // No behavior tree service.
                }

                try
                {
                    events::ai::GetBehaviorTreeRuntimeStateQuery query;
                    query.entity = owner.handle;
                    const behaviortree::BTRuntimeState state = dispatcher.query(query);
                    if (!state.attached)
                    {
                        continue;
                    }
                    const std::unordered_set<std::string> routeKeys = followRouteKeys(state.tree.get());
                    for (const auto& [key, value] : state.snapshot.blackboard)
                    {
                        if (const auto* target = std::get_if<services::EntityHandle>(&value))
                        {
                            add(target->id, owner, key, "live");
                        }
                        else if (const auto* number = std::get_if<int32_t>(&value);
                                 number != nullptr && *number >= 0 && routeKeys.contains(key))
                        {
                            add(static_cast<uint64_t>(static_cast<uint32_t>(*number)), owner, key, "live");
                        }
                    }
                }
                catch (const std::runtime_error&)
                {
                    // No behavior tree service.
                }
            }
            return followers;
        }

        nlohmann::json routeJson(const services::EntityData& route, nlohmann::json followers)
        {
            RouteView view = viewOf(route);
            return {
                {"entity", toId(route.handle)},
                {"name", route.name},
                {"points", std::move(view.points)},
                {"markerIds", std::move(view.markerIds)},
                {"followers", std::move(followers)},
                {"warnings", routeWarnings(route, view.markers)}
            };
        }

        // ------------------------------------------------------------------
        // route_create
        // ------------------------------------------------------------------

        void registerRouteCreate(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "route_create";
            tool.title = "Create patrol route";
            tool.description =
                "Create a patrol route: an entity at the scene root (origin, identity transform) with a NavRoute "
                "component, and one marker child per point (WP0, WP1, ... in order). The behavior tree's FollowRoute "
                "task walks the markers in that order (assign the route to an entity's tree with bt_assign "
                "{blackboard: {route: <route id>}}; vf://docs/navigation). points are world [x, y, z] (2-256). "
                "snapToNavmesh (default true) moves each point onto the navmesh as FollowRoute will (within 5, 25 or "
                "100 m); pass false for a flying agent's route (FollowRoute snaps it to the volumetric volume instead) "
                "or to keep the points as given. The route is drawn in the viewport while the navmesh overlay is on. "
                "One undo step. Refused in Play mode, World mode and while a scene load is pending. Returns {route, "
                "name, markers, points, warnings}.";
            tool.inputSchema = closedObject({
                {"name", schema::string("Route entity name (scripts find routes by name).")},
                {"points", pointsSchema("Marker positions in patrol order (2-256).")},
                {"snapToNavmesh", schema::boolean("Snap the points onto the navmesh as FollowRoute does. Default true; "
                                                  "false for a flying agent's route.")}
            }, {"name", "points"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"name", "points", "snapToNavmesh"}, "route_create");
                if (const std::optional<std::string> blocked = editBlocker("create a route", worldModeReason))
                {
                    return ToolResult::error(*blocked);
                }

                const ArgReader reader(args);
                const std::string name = reader.requireString("name");
                if (name.empty())
                {
                    throw ArgError("argument 'name' must not be empty");
                }
                const std::vector<glm::vec3> given = requirePoints(reader.raw("points"), minPoints, maxPoints, "points");
                const bool snap = reader.optBool("snapToNavmesh", true);

                auto& dispatcher = events::EventDispatcher::instance();
                std::vector<std::string> warnings;
                events::scene::FindEntitiesByNameQuery sameName;
                sameName.name = name;
                if (const std::size_t others = dispatcher.query(sameName).size(); others > 0)
                {
                    warnings.push_back(std::to_string(others) + " other entit" + (others == 1 ? "y is" : "ies are") +
                                       " named '" + name + "': a script that finds the route by name "
                                       "(Entity::findByName) takes the first.");
                }
                const std::vector<glm::vec3> points = snap ? snapPoints(given, warnings) : given;

                events::scene::CreateEntityCommand create;
                create.name = name;
                const services::EntityHandle route = dispatcher.execute(create);
                if (!route.isValid())
                {
                    return ToolResult::error("CreateEntity failed");
                }

                nlohmann::json markers = nlohmann::json::array();
                try
                {
                    events::scene::AddNavRouteComponentCommand addRoute;
                    addRoute.entity = route;
                    if (!dispatcher.execute(addRoute))
                    {
                        throw std::runtime_error("AddNavRouteComponent failed");
                    }
                    const std::optional<services::EntityData> created = findEntity(route);
                    if (!created.has_value())
                    {
                        throw std::runtime_error("the new route entity disappeared");
                    }
                    // Identity at the scene root; composed anyway, so the markers land on the points regardless.
                    const glm::mat4 toLocal = worldToRoute(*created);
                    for (std::size_t i = 0; i < points.size(); ++i)
                    {
                        markers.push_back(toId(createMarker(route, i, localPoint(toLocal, points[i]))));
                    }

                    // One entry for the whole route: its prefab snapshot carries the NavRoute and every marker.
                    undo::UndoRecorder recorder("MCP: Create route (" + name + ")");
                    recorder.add(undo::EntityLifetimeUndo::capture(undo::EntityLifetimeUndo::Kind::Created, route));
                    recorder.push();
                }
                catch (...)
                {
                    deleteQuietly(route);  // not undoable: don't leave a half-built route behind
                    throw;
                }

                nlohmann::json pointList = nlohmann::json::array();
                for (const glm::vec3& point : points)
                {
                    pointList.push_back(pointJson(point));
                }
                nlohmann::json out{
                    {"route", toId(route)},
                    {"name", name},
                    {"markers", std::move(markers)},
                    {"points", std::move(pointList)},
                    {"warnings", warnings}
                };
                std::string text = "Created route '" + name + "' (entity " + idText(route) + ") with " +
                                   std::to_string(points.size()) + " markers. Give it to a behavior tree with bt_assign "
                                   "{entity, tree, blackboard: {route: " + idText(route) + "}}.";
                if (!warnings.empty())
                {
                    text += " " + std::to_string(warnings.size()) + " warning(s).";
                }
                return reply(std::move(out), std::move(text));
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // route_set
        // ------------------------------------------------------------------

        void registerRouteSet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "route_set";
            tool.title = "Change patrol route";
            tool.description =
                "Change a route (route_create / route_get): points (2-256 world [x, y, z]) replace the markers' "
                "positions in order: existing markers move, extra points add markers (WP<n>), and when there are "
                "fewer points the last markers are deleted. name renames the route. snapToNavmesh (default true) "
                "snaps the points onto the navmesh as route_create does; snapToNavmesh: true without points snaps the "
                "current markers again (e.g. after navmesh_bake). One undo step, none when nothing changes. Refused "
                "in Play mode, World mode and while a scene load is pending. Returns {route, name, markers, points, "
                "changed, warnings}.";
            tool.inputSchema = closedObject({
                {"route", schema::entity("The route entity (route_get lists them).")},
                {"points", pointsSchema("New marker positions in patrol order (2-256).")},
                {"name", schema::string("New route name.")},
                {"snapToNavmesh", schema::boolean("Snap the points onto the navmesh. Default true; alone (true) it "
                                                  "snaps the current markers.")}
            }, {"route"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"route", "points", "name", "snapToNavmesh"}, "route_set");
                if (const std::optional<std::string> blocked = editBlocker("change a route", worldModeReason))
                {
                    return ToolResult::error(*blocked);
                }

                const ArgReader reader(args);
                const services::EntityData route = requireRoute(reader, "route");
                std::optional<std::string> name;
                if (reader.has("name"))
                {
                    name = reader.requireString("name");
                    if (name->empty())
                    {
                        throw ArgError("argument 'name' must not be empty");
                    }
                }
                std::optional<std::vector<glm::vec3>> given;
                if (reader.has("points"))
                {
                    given = requirePoints(reader.raw("points"), minPoints, maxPoints, "points");
                }
                const bool snapGiven = reader.has("snapToNavmesh");
                const bool snap = reader.optBool("snapToNavmesh", true);
                const bool resnap = !given.has_value() && snapGiven && snap;
                if (!given.has_value() && !name.has_value() && !resnap)
                {
                    throw ArgError("route_set needs 'points', 'name', or snapToNavmesh: true (to snap the current "
                                   "markers again)");
                }

                const std::vector<services::EntityData> markers = routeMarkers(route);
                const glm::mat4 toLocal = worldToRoute(route);
                std::vector<std::string> warnings;
                std::optional<std::vector<glm::vec3>> targets;
                if (given.has_value())
                {
                    targets = snap ? snapPoints(*given, warnings) : *given;
                }
                else if (resnap)
                {
                    const glm::mat4 world = worldMatrix(route);
                    std::vector<glm::vec3> current;
                    current.reserve(markers.size());
                    for (const services::EntityData& marker : markers)
                    {
                        current.push_back(glm::vec3((world * localMatrix(marker.localTransform))[3]));
                    }
                    targets = snapPoints(current, warnings);
                }

                // Every write is recorded as it happens; a failure part-way undoes what was done.
                auto& dispatcher = events::EventDispatcher::instance();
                undo::UndoRecorder recorder("MCP: Set route (" + route.name + ")");
                bool changed = false;
                try
                {
                    if (name.has_value() && *name != route.name)
                    {
                        events::scene::SetEntityNameCommand rename;
                        rename.entity = route.handle;
                        rename.newName = *name;
                        dispatcher.execute(rename);
                        recorder.add(std::make_unique<undo::RenameUndo>(toId(route.handle), route.name, *name));
                        changed = true;
                    }
                    if (targets.has_value())
                    {
                        const std::size_t reused = std::min(markers.size(), targets->size());
                        for (std::size_t i = 0; i < reused; ++i)
                        {
                            const services::TransformData before = markers[i].localTransform;
                            services::TransformData after = before;
                            after.position = localPoint(toLocal, (*targets)[i]);
                            if (after == before)
                            {
                                continue;
                            }
                            events::scene::SetTransformCommand move;
                            move.entity = markers[i].handle;
                            move.transform = after;
                            dispatcher.execute(move);
                            recorder.add(std::make_unique<undo::TransformUndo>(toId(markers[i].handle), markers[i].name,
                                                                                before, after));
                            changed = true;
                        }
                        // The last markers first: each snapshot keeps its sibling index for the undo.
                        for (std::size_t i = markers.size(); i > targets->size(); --i)
                        {
                            const services::EntityData& marker = markers[i - 1];
                            std::unique_ptr<undo::EntityLifetimeUndo> removal =
                                undo::EntityLifetimeUndo::capture(undo::EntityLifetimeUndo::Kind::Deleted, marker.handle);
                            events::scene::DeleteEntityCommand remove;
                            remove.entity = marker.handle;
                            if (!dispatcher.execute(remove))
                            {
                                throw std::runtime_error("deleting marker " + idText(marker.handle) + " ('" +
                                                         marker.name + "') failed");
                            }
                            recorder.add(std::move(removal));
                            changed = true;
                        }
                        for (std::size_t i = markers.size(); i < targets->size(); ++i)
                        {
                            const services::EntityHandle marker = createMarker(route.handle, i,
                                                                               localPoint(toLocal, (*targets)[i]));
                            try
                            {
                                recorder.add(undo::EntityLifetimeUndo::capture(undo::EntityLifetimeUndo::Kind::Created,
                                                                               marker));
                            }
                            catch (...)
                            {
                                deleteQuietly(marker);
                                throw;
                            }
                            changed = true;
                        }
                    }
                }
                catch (...)
                {
                    recorder.rollback();
                    throw;
                }
                recorder.push();

                const std::optional<services::EntityData> updated = findEntity(route.handle);
                if (!updated.has_value())
                {
                    throw std::runtime_error("Route " + idText(route.handle) + " disappeared");
                }
                RouteView view = viewOf(*updated);
                for (std::string& warning : routeWarnings(*updated, view.markers))
                {
                    warnings.push_back(std::move(warning));
                }
                nlohmann::json out{
                    {"route", toId(updated->handle)},
                    {"name", updated->name},
                    {"markers", std::move(view.markerIds)},
                    {"points", std::move(view.points)},
                    {"changed", changed},
                    {"warnings", warnings}
                };
                std::string text = changed ? "Updated route '" + updated->name + "' (" + std::to_string(view.markers) +
                                                 " markers) as one undo step."
                                           : "Route '" + updated->name + "' unchanged.";
                if (!warnings.empty())
                {
                    text += " " + std::to_string(warnings.size()) + " warning(s).";
                }
                return reply(std::move(out), std::move(text));
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // route_get
        // ------------------------------------------------------------------

        void registerRouteGet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "route_get";
            tool.title = "Get patrol routes";
            tool.description =
                "Describe patrol routes: 'route' = one route's entity id; omit it for every route of the scene. Each "
                "is {entity, name, points (the markers' world positions in FollowRoute order), markerIds, followers, "
                "warnings}. followers [{entity, name, key, source}] are the entities whose behavior tree start values "
                "(bt_assign / bt_set_blackboard {entity}) hold the route as an Entity value (source startValue) and, "
                "in Play mode, whose live blackboard holds it in an Entity key or in the Int key a FollowRoute node "
                "reads (source live); a script that finds a route by name is not listed. Works in any mode. Returns "
                "the route, or {routes, count}.";
            tool.inputSchema = closedObject({{"route", schema::entity("One route entity. Omit for every route.")}});
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"route"}, "route_get");
                const ArgReader reader(args);
                const bool one = reader.has("route");
                std::vector<services::EntityData> routes;
                if (one)
                {
                    routes.push_back(requireRoute(reader, "route"));
                }
                else
                {
                    routes = entitiesWith(services::ComponentTypeId::NavRoute);
                }

                std::unordered_map<uint64_t, nlohmann::json> followers = routeFollowers(routes);
                nlohmann::json list = nlohmann::json::array();
                for (const services::EntityData& route : routes)
                {
                    list.push_back(routeJson(route, std::move(followers[route.handle.id])));
                }

                if (one)
                {
                    const nlohmann::json& route = list.front();
                    return reply(route, "Route '" + route.at("name").get<std::string>() + "': " +
                                            std::to_string(route.at("markerIds").size()) + " markers, " +
                                            std::to_string(route.at("followers").size()) + " follower(s).");
                }
                const std::size_t count = list.size();
                return reply({{"routes", std::move(list)}, {"count", count}},
                             std::to_string(count) + " route(s) in the scene.");
            };
            registry.add(std::move(tool));
        }
    }

    void registerRouteTools(ToolRegistry& registry, const ToolContext&)
    {
        registerRouteCreate(registry);
        registerRouteSet(registry);
        registerRouteGet(registry);
    }
}
