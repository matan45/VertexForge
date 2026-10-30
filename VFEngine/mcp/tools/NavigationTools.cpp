#include "CoreTools.hpp"
#include "EnvironmentToolSupport.hpp"
#include "NavigationToolSupport.hpp"
#include "PathSandbox.hpp"
#include "SettingsTable.hpp"
#include "TerrainToolSupport.hpp"
#include "ToolHelpers.hpp"
#include "../protocol/ArgReader.hpp"

#include "events/EventDispatcher.hpp"
#include "events/navmesh/NavmeshAuthoringEvents.hpp"
#include "events/navmesh/NavmeshEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"

#include <glm/glm.hpp>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

// VK-1660 navigation tools, part 1: navmesh_bake, navmesh_get_info, navmesh_clear and navmesh_find_path (+
// registerNavigationTools, which also registers route_create / route_set / route_get from RouteTools.cpp). The bake
// is the navmesh service's ticketed bake (NavmeshAuthoringEvents.hpp) followed by SaveNavmeshTiledCommand, so what
// an agent bakes is on disk and referenced by the scene root's NavmeshComponent. None of these tools is undoable.
namespace mcp::tools
{
    namespace
    {
        namespace fs = std::filesystem;

        // navmesh_bake's hops. The prepare hop collects the scene geometry on the main thread, as the Navigation
        // window's Bake does; the bake itself runs on a worker and is polled; the save hop writes every tile.
        constexpr std::chrono::milliseconds prepareTimeout{60000};
        constexpr std::chrono::milliseconds bakeDeadline{300000};
        constexpr std::chrono::milliseconds saveTimeout{120000};
        constexpr std::chrono::milliseconds pollInterval{100};
        constexpr std::chrono::milliseconds bakeTimeout = prepareTimeout + bakeDeadline + saveTimeout;

        // navmesh_find_path. Each end snaps to the navmesh within snapRadius before the path is searched.
        constexpr double defaultSnapRadius = 5.0;
        constexpr double minSnapRadius = 0.5;
        constexpr double maxSnapRadius = 20.0;
        // A path end farther than this from its snapped endpoint did not reach it: a hierarchical (long, tiled)
        // path never sets NavPath::isPartial and may skip a stretch it cannot cross.
        constexpr float endpointTolerance = 0.5f;
        // An endpoint snapped farther than this gets a warning.
        constexpr float snapWarningDistance = 1.0f;

        constexpr const char* worldBakeReason =
            "a world navmesh is baked sector by sector in the Navigation window (World Bake), not here.";

        constexpr const char* geometryHint =
            "The bake rasterizes the terrain (include.terrain), every Mesh entity (staticMeshes), Box / ConvexMesh / "
            "TriangleMesh colliders of entities without a Mesh (colliders) and nav-contributing foliage on loaded "
            "terrain tiles (foliage); NavmeshAgent, VolumetricAgent and Controller entities, Dynamic rigid bodies and "
            "trigger colliders never count. Add walkable ground (a terrain, or a Mesh such as a floor), check "
            "include, and with bounds make sure the box holds the ground.";

        // ------------------------------------------------------------------
        // navmesh_bake
        // ------------------------------------------------------------------

        nlohmann::json bakeSchema()
        {
            nlohmann::json properties = env::schemaJson(navtool::bakeTable(), "bake settings").at("properties");
            properties["include"] = env::schemaJson(navtool::includeTable(),
                                                    "Which geometry the bake rasterizes; keys not given keep the last "
                                                    "bake's values.");
            nlohmann::json bounds = closedObject({
                {"min", schema::vec3("World [x, y, z] corner.")},
                {"max", schema::vec3("World [x, y, z] corner, above min on every axis.")}
            }, {"min", "max"});
            bounds["description"] = "Limit the bake to the whole tiles this box overlaps; on Y it is a floor and a "
                                    "ceiling for the geometry. Omit to bake the whole scene.";
            properties["bounds"] = std::move(bounds);
            properties["directory"] = schema::string(
                "Project-relative folder for the navmesh files (index.vfNavIndex + tiles). Default: the folder of the "
                "scene's current navmesh, else navmesh/<scene file name>.");
            properties["showDebug"] = schema::boolean(
                "Turn the navmesh overlay on afterwards (viewport_screenshot shows it). Default true.");
            return closedObject(std::move(properties));
        }

        events::navmesh::NavmeshBakeBounds readBounds(const nlohmann::json& value)
        {
            env::rejectUnknownKeys(value, {"min", "max"}, "bounds");
            if (!value.contains("min") || !value.contains("max"))
            {
                throw ArgError("'bounds' needs both min and max ([x, y, z] world corners)");
            }
            events::navmesh::NavmeshBakeBounds bounds;
            bounds.min = navtool::requirePoint(value.at("min"), "bounds.min");
            bounds.max = navtool::requirePoint(value.at("max"), "bounds.max");
            if (!(bounds.min.x < bounds.max.x && bounds.min.y < bounds.max.y && bounds.min.z < bounds.max.z))
            {
                throw ArgError("'bounds.min' must be below 'bounds.max' on every axis (got min " +
                               navtool::pointJson(bounds.min).dump() + ", max " + navtool::pointJson(bounds.max).dump() +
                               ")");
            }
            return bounds;
        }

        struct BakeFolder
        {
            fs::path directory;
            bool untitled = false;  // the scene has no file: the shared navmesh/untitled folder
        };

        // 'directory' (sandboxed in the project), else the folder of the scene's current navmesh, else
        // navmesh/<scene file name> (agent paths are relative to the project's assets folder).
        BakeFolder chooseFolder(const ArgReader& reader, const events::navmesh::NavmeshStatus& status)
        {
            const fs::path root = terrainProjectRoot();
            if (reader.has("directory"))
            {
                const std::string requested = reader.requireString("directory");
                std::string error;
                const std::optional<fs::path> resolved = resolveInside(root, requested, error);
                if (!resolved.has_value())
                {
                    throw ArgError("argument 'directory': " + error);
                }
                std::error_code ec;
                if (fs::exists(*resolved, ec) && !fs::is_directory(*resolved, ec))
                {
                    throw ArgError("argument 'directory': '" + requested + "' is a file, not a folder");
                }
                return {*resolved, false};
            }
            if (!status.indexPath.empty())
            {
                const fs::path index = terrainEnginePath(root, status.indexPath);
                if (index.has_parent_path())
                {
                    return {index.parent_path(), false};
                }
            }
            const std::string scenePath =
                events::EventDispatcher::instance().query(events::scene::GetCurrentScenePathQuery{});
            const fs::path stem = scenePath.empty() ? fs::path() : fs::path(scenePath).stem();
            const bool untitled = stem.empty();
            // root = ProjectConfig::workingDirectory, which IS the project's assets folder.
            return {root / "navmesh" / (untitled ? fs::path("untitled") : stem), untitled};
        }

        // The folder as an engine path (narrow, like every engine path). Checked before the bake starts, so an
        // unrepresentable folder fails up front.
        std::string engineDirectory(const fs::path& directory)
        {
            try
            {
                return directory.generic_string();
            }
            catch (const std::exception&)
            {
                throw ArgError("the navmesh folder '" + pathToUtf8(directory) + "' cannot be written through the "
                               "engine's file paths; choose a 'directory' without characters outside the system code "
                               "page");
            }
        }

        // The prepare hop (main thread): every refusal and argument check, the folder, then the bake request.
        nlohmann::json startBake(const nlohmann::json& args)
        {
            if (const std::optional<std::string> blocked = navtool::editBlocker("bake the navmesh", worldBakeReason))
            {
                return {{"error", *blocked}};
            }
            auto& dispatcher = events::EventDispatcher::instance();
            const events::navmesh::NavmeshStatus status = dispatcher.query(events::navmesh::GetNavmeshStatusQuery{});
            if (status.bakeRunning)
            {
                return {{"error", "A navmesh bake is already running (started in the Navigation window or by another "
                                  "call); wait until navmesh_get_info reports bake.state idle, then retry"}};
            }

            // The last bake's settings with the keys given.
            const ArgReader reader(args);
            nlohmann::json settingsPatch = nlohmann::json::object();
            for (const env::FieldDesc<types::NavmeshBakeSettings>& field : navtool::bakeTable().fields)
            {
                const std::string key(field.key);
                if (reader.has(key.c_str()))
                {
                    settingsPatch[key] = reader.raw(key.c_str());
                }
            }
            types::NavmeshBakeSettings settings = env::strictPatch(
                navtool::bakeTable(), dispatcher.query(events::navmesh::GetNavmeshSettingsQuery{}), settingsPatch, "");
            if (reader.has("include"))
            {
                settings = env::strictPatch(navtool::includeTable(), settings, reader.raw("include"), "include");
            }
            std::optional<events::navmesh::NavmeshBakeBounds> bounds;
            if (reader.has("bounds"))
            {
                bounds = readBounds(reader.raw("bounds"));
            }
            const BakeFolder folder = chooseFolder(reader, status);
            (void)engineDirectory(folder.directory);

            std::vector<std::string> warnings;
            if (folder.untitled)
            {
                warnings.push_back("The scene has never been saved, so the navmesh went to navmesh/untitled, a "
                                   "folder every unsaved scene shares. Save the scene (scene_save {path}), then bake "
                                   "again to give it a folder of its own.");
            }
            // NavmeshAdapterBuildOps: walkableClimb = floor(agentMaxClimb / cellHeight).
            if (settings.agentMaxClimb < settings.cellHeight)
            {
                warnings.push_back("stepHeight " + navtool::numberText(settings.agentMaxClimb) + " is below cellHeight " +
                                   navtool::numberText(settings.cellHeight) +
                                   ": the climb is rounded down to whole cells, so agents cannot step onto anything. "
                                   "Raise stepHeight or lower cellHeight.");
            }
            if (bounds.has_value())
            {
                warnings.push_back("bounds limit the navmesh to the whole tiles the box overlaps (each " +
                                   navtool::numberText(static_cast<float>(settings.tileSize) * settings.cellSize) +
                                   " m wide); on Y the box is a floor and a ceiling. Agents outside those tiles have "
                                   "no navmesh.");
            }

            events::navmesh::RequestNavmeshBakeCommand request;
            request.settings = settings;
            request.bounds = bounds;
            const events::navmesh::NavmeshBakeTicket ticket = dispatcher.execute(request);
            if (!ticket.accepted)
            {
                std::string refusal = "The navmesh bake was refused: " +
                                      (ticket.error.empty() ? std::string("no reason given") : ticket.error);
                if (ticket.error.find("geometry") != std::string::npos)
                {
                    refusal += ". " + std::string(geometryHint);
                }
                return {{"error", refusal}};
            }
            return {{"ticket", ticket.ticket}, {"directory", pathToUtf8(folder.directory)}, {"warnings", warnings}};
        }

        nlohmann::json bakeOutcome(uint64_t ticket)
        {
            events::navmesh::GetNavmeshBakeOutcomeQuery query;
            query.ticket = ticket;
            const events::navmesh::NavmeshBakeOutcome outcome = events::EventDispatcher::instance().query(query);
            const char* state = "unknown";
            switch (outcome.state)
            {
            case events::navmesh::NavmeshBakeState::Running:
                state = "running";
                break;
            case events::navmesh::NavmeshBakeState::Succeeded:
                state = "succeeded";
                break;
            case events::navmesh::NavmeshBakeState::Failed:
                state = "failed";
                break;
            case events::navmesh::NavmeshBakeState::Unknown:
                break;
            }
            return {
                {"state", state},
                {"progress", env::floatJson(outcome.progress)},
                {"stage", outcome.stage},
                {"message", outcome.message},
                {"stats", navtool::statsJson(outcome.stats)}
            };
        }

        // As the editor's Debug > Show Navmesh: the overlay draws the debug mesh it was last given.
        void showOverlay()
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::render::SetShowNavmeshDebugCommand show;
            show.show = true;
            dispatcher.execute(show);

            events::navmesh::NavmeshDebugMeshResult mesh = dispatcher.query(events::navmesh::GetNavmeshDebugMeshQuery{});
            if (!mesh.vertices.empty() && !mesh.indices.empty())
            {
                events::render::UpdateNavmeshDebugMeshCommand update;
                update.vertices = std::move(mesh.vertices);
                update.indices = std::move(mesh.indices);
                dispatcher.execute(update);
            }
        }

        // The NavmeshAgents this bake is too small for, as one warning; nullopt when none is.
        std::optional<std::string> oversizedAgents(const types::NavmeshBakeSettings& settings)
        {
            constexpr int listed = 8;
            int count = 0;
            std::string names;
            for (const services::EntityData& agent : navtool::entitiesWith(services::ComponentTypeId::NavmeshAgent))
            {
                events::scene::GetNavmeshAgentDataQuery query;
                query.entity = agent.handle;
                const std::optional<services::NavmeshAgentData> data = events::EventDispatcher::instance().query(query);
                if (!data.has_value() || (data->radius <= settings.agentRadius + navtool::sizeTolerance &&
                                          data->height <= settings.agentHeight + navtool::sizeTolerance))
                {
                    continue;
                }
                if (++count <= listed)
                {
                    names += (names.empty() ? "" : ", ") + std::to_string(toId(agent.handle)) + " '" + agent.name +
                             "' (radius " + navtool::numberText(data->radius) + ", height " +
                             navtool::numberText(data->height) + ")";
                }
            }
            if (count == 0)
            {
                return std::nullopt;
            }
            return std::to_string(count) + " NavmeshAgent(s) are larger than this bake (agentRadius " +
                   navtool::numberText(settings.agentRadius) + ", agentHeight " +
                   navtool::numberText(settings.agentHeight) + "): " + names + (count > listed ? ", ..." : "") +
                   ". Bake with navmesh_bake {agentRadius, agentHeight} covering them, or make them smaller "
                   "(component_set NavmeshAgent).";
        }

        // The save hop (main thread): SaveNavmeshTiledCommand into the folder, then the overlay.
        nlohmann::json saveBake(const std::string& directoryUtf8, bool showDebug)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            const events::navmesh::NavmeshStatus before = dispatcher.query(events::navmesh::GetNavmeshStatusQuery{});
            if (before.bakeRunning)
            {
                return {{"error", "another navmesh bake started before this one was saved"}};
            }
            if (!before.hasNavmesh)
            {
                return {{"error", "the navmesh is gone: a navmesh clear or a scene load ran before it was saved"}};
            }

            const std::optional<fs::path> root = tryTerrainProjectRoot();
            const fs::path directory = pathFromUtf8(directoryUtf8);
            events::navmesh::SaveNavmeshTiledCommand save;
            save.directory = engineDirectory(directory);
            if (!dispatcher.execute(save))
            {
                return {{"error", "writing the navmesh files into '" + projectRelativeUtf8(root, directory) +
                                  "' failed (logs_read shows why)"}};
            }

            std::vector<std::string> warnings;
            bool overlay = false;
            if (showDebug)
            {
                try
                {
                    showOverlay();
                    overlay = true;
                }
                catch (const std::exception& e)
                {
                    warnings.push_back(std::string("The navmesh overlay could not be turned on: ") + e.what());
                }
            }
            const events::navmesh::NavmeshStatus status = dispatcher.query(events::navmesh::GetNavmeshStatusQuery{});
            if (std::optional<std::string> oversized = oversizedAgents(status.settings))
            {
                warnings.push_back(std::move(*oversized));
            }
            return {
                {"directory", projectRelativeUtf8(root, directory)},
                {"index", projectRelativeUtf8(root, directory / "index.vfNavIndex")},
                {"stats", navtool::statsJson(status.stats)},
                {"settings", navtool::bakeSettingsJson(status.settings)},
                {"overlay", overlay},
                {"warnings", warnings}
            };
        }

        void registerNavmeshBake(ToolRegistry& registry, const ToolContext& context)
        {
            ToolDef tool;
            tool.name = "navmesh_bake";
            tool.title = "Bake navmesh";
            tool.description =
                "Bake the scene's navmesh (the walkable surface NavmeshAgent entities move on) and SAVE it: the files "
                "(index.vfNavIndex + tiles) go to 'directory' (project-relative), else the folder of the navmesh the "
                "scene already uses, else navmesh/<scene file name>, and the scene root's NavmeshComponent "
                "points at them, so run scene_save afterwards to keep that reference. Settings not given keep the last "
                "bake's values (navmesh_get_info shows them): agentRadius / agentHeight at least the largest "
                "NavmeshAgent's radius / height, maxSlope the steepest walkable ground (degrees), stepHeight the "
                "highest step (metres; at least cellHeight), cellSize / cellHeight the voxel size (smaller = more "
                "precise and slower; cellSize about agentRadius / 2). include picks the geometry: terrain, "
                "staticMeshes (every Mesh entity), colliders (Box / ConvexMesh / TriangleMesh colliders of entities "
                "without a Mesh), foliage; agents, Controllers and Dynamic rigid bodies never count. bounds {min, max} "
                "limits the bake to the whole tiles the box overlaps. showDebug (default true) turns the navmesh "
                "overlay on. Takes seconds while the editor keeps running; a bake producing no polygons is an error "
                "and saves nothing. Refused in Play mode, World mode (bake a world in the Navigation window), while a "
                "scene load is pending and while another bake runs. Not undoable. Returns {tiles, polygons, vertices, "
                "bounds, settings, saved {directory, index}, overlay, durationMs, warnings}.";
            tool.inputSchema = bakeSchema();
            // Worker: the bake runs for seconds on a background job; only the prepare, poll and save hops touch the
            // main thread, so the editor keeps rendering meanwhile.
            tool.affinity = ThreadAffinity::Worker;
            tool.timeout = bakeTimeout;
            tool.destructive = true;
            tool.handler = [context](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"agentRadius", "agentHeight", "maxSlope", "stepHeight", "cellSize",
                                              "cellHeight", "include", "bounds", "directory", "showDebug"},
                                       "navmesh_bake");
                const ArgReader reader(args);
                const bool showDebug = reader.optBool("showDebug", true);
                const auto started = std::chrono::steady_clock::now();

                const nlohmann::json request = context.runOnMain([args]() -> nlohmann::json
                {
                    return startBake(args);
                }, prepareTimeout);
                if (request.contains("error"))
                {
                    return ToolResult::error(request.at("error").get<std::string>());
                }
                const uint64_t ticket = request.at("ticket").get<uint64_t>();
                const std::string directory = request.at("directory").get<std::string>();
                std::vector<std::string> warnings = request.at("warnings").get<std::vector<std::string>>();

                // The service harvests the bake in its frame task; poll until the ticket leaves Running.
                nlohmann::json outcome;
                const auto deadline = std::chrono::steady_clock::now() + bakeDeadline;
                while (true)
                {
                    outcome = context.runOnMain([ticket]() -> nlohmann::json { return bakeOutcome(ticket); });
                    if (outcome.at("state").get<std::string>() != "running")
                    {
                        break;
                    }
                    if (std::chrono::steady_clock::now() >= deadline)
                    {
                        return ToolResult::error(
                            "The navmesh bake did not finish within " + std::to_string(bakeDeadline.count() / 1000) +
                            " s; it keeps running in the editor but this call will not save it. Once navmesh_get_info "
                            "reports bake.state idle, run navmesh_bake again (or save it from the Navigation window).");
                    }
                    std::this_thread::sleep_for(pollInterval);
                }

                const std::string state = outcome.at("state").get<std::string>();
                if (state == "failed")
                {
                    const std::string why = outcome.at("message").get<std::string>();
                    return ToolResult::error("The navmesh bake failed: " +
                                             (why.empty() ? std::string("no reason given") : why));
                }
                if (state != "succeeded")
                {
                    return ToolResult::error("The navmesh bake's result was lost (the editor no longer knows its "
                                             "ticket); run navmesh_bake again");
                }
                if (outcome.at("stats").at("polygons").get<int64_t>() <= 0)
                {
                    return ToolResult::error(
                        "The bake produced no walkable polygons, so nothing was saved: the navmesh files on disk are "
                        "unchanged, and the live navmesh stays empty until the next bake or scene load. Check that the "
                        "ground is less steep than maxSlope, that agentRadius / agentHeight fit the open space and, "
                        "with bounds, that the box holds the ground. " + std::string(geometryHint));
                }

                const nlohmann::json saved = context.runOnMain([directory, showDebug]() -> nlohmann::json
                {
                    return saveBake(directory, showDebug);
                }, saveTimeout);
                if (saved.contains("error"))
                {
                    return ToolResult::error("The navmesh was baked (" +
                                             outcome.at("stats").at("polygons").dump() +
                                             " polygons) but NOT saved: " + saved.at("error").get<std::string>() +
                                             ". It drives agents now but is not on disk; run navmesh_bake again.");
                }
                for (const nlohmann::json& warning : saved.at("warnings"))
                {
                    warnings.push_back(warning.get<std::string>());
                }

                const int64_t durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
                const nlohmann::json& stats = saved.at("stats");
                nlohmann::json structured{
                    {"tiles", stats.at("tiles")},
                    {"polygons", stats.at("polygons")},
                    {"vertices", stats.at("vertices")},
                    {"bounds", stats.at("bounds")},
                    {"settings", saved.at("settings")},
                    {"saved", {{"directory", saved.at("directory")}, {"index", saved.at("index")}}},
                    {"overlay", saved.at("overlay")},
                    {"durationMs", durationMs},
                    {"warnings", warnings}
                };
                std::string text = "Baked and saved the navmesh: " + stats.at("tiles").dump() + " tile(s), " +
                                   stats.at("polygons").dump() + " polygons in " +
                                   navtool::numberText(static_cast<float>(durationMs) / 1000.0f) + " s, files in " +
                                   saved.at("directory").get<std::string>() +
                                   ". Run scene_save so the scene keeps referencing them.";
                if (!warnings.empty())
                {
                    text += " " + std::to_string(warnings.size()) + " warning(s).";
                }
                return reply(std::move(structured), std::move(text));
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // navmesh_get_info
        // ------------------------------------------------------------------

        void registerNavmeshGetInfo(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "navmesh_get_info";
            tool.title = "Navmesh info";
            tool.description =
                "Describe the scene's navmesh: hasNavmesh, the bake in progress (bake {state running | idle, progress, "
                "stage}), its size (stats {tiles, polygons, vertices, bounds}), the settings it was baked with "
                "(navmesh_bake's keys), the saved files (asset {index: project-relative, unsavedChanges: a bake that "
                "was never saved}), every NavmeshAgent (entity, name, radius, height, baseOffset, inCrowd, its feet = "
                "world position minus baseOffset, onNavmesh, nearest, offset in metres, warnings), the routes "
                "({entity, name, markers}; route_get has the details), streaming and worldMode. Works in any mode.";
            tool.inputSchema = closedObject(nlohmann::json::object());
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {}, "navmesh_get_info");
                auto& dispatcher = events::EventDispatcher::instance();
                const events::navmesh::NavmeshStatus status = dispatcher.query(events::navmesh::GetNavmeshStatusQuery{});

                // The progress query keeps reporting the previous bake once it ended; it counts only while running.
                nlohmann::json bake{{"state", status.bakeRunning ? "running" : "idle"}, {"progress", nullptr},
                                    {"stage", nullptr}};
                if (status.bakeRunning)
                {
                    const types::NavmeshBakeProgress progress = dispatcher.query(events::navmesh::GetBakeProgressQuery{});
                    bake["progress"] = env::floatJson(progress.progress);
                    bake["stage"] = progress.currentStage;
                }

                const std::optional<fs::path> root = tryTerrainProjectRoot();
                const nlohmann::json index = status.indexPath.empty()
                    ? nlohmann::json(nullptr)
                    : nlohmann::json(projectRelativeUtf8(root, terrainEnginePath(root, status.indexPath)));

                nlohmann::json agents = nlohmann::json::array();
                for (const services::EntityData& entity : navtool::entitiesWith(services::ComponentTypeId::NavmeshAgent))
                {
                    events::scene::GetNavmeshAgentDataQuery query;
                    query.entity = entity.handle;
                    const std::optional<services::NavmeshAgentData> data = dispatcher.query(query);
                    if (!data.has_value())
                    {
                        continue;
                    }
                    const glm::vec3 feet = navtool::worldPosition(entity) - glm::vec3(0.0f, data->baseOffset, 0.0f);
                    nlohmann::json agent{
                        {"entity", toId(entity.handle)},
                        {"name", entity.name},
                        {"radius", env::floatJson(data->radius)},
                        {"height", env::floatJson(data->height)},
                        {"baseOffset", env::floatJson(data->baseOffset)},
                        {"inCrowd", data->inCrowd},
                        {"feet", navtool::pointJson(feet)}
                    };
                    agent.update(navtool::probeJson(status.hasNavmesh ? navtool::probe(feet, navtool::agentProbeRadius)
                                                                      : navtool::Probe{},
                                                    feet));
                    agent["warnings"] = navtool::agentWarnings(entity, *data);
                    agents.push_back(std::move(agent));
                }

                nlohmann::json routes = nlohmann::json::array();
                for (const services::EntityData& route : navtool::entitiesWith(services::ComponentTypeId::NavRoute))
                {
                    routes.push_back(nlohmann::json{{"entity", toId(route.handle)}, {"name", route.name},
                                                    {"markers", route.children.size()}});
                }

                const bool worldMode = isWorldModeActive();
                nlohmann::json out{
                    {"hasNavmesh", status.hasNavmesh},
                    {"bake", std::move(bake)},
                    {"stats", navtool::statsJson(status.stats)},
                    {"settings", navtool::bakeSettingsJson(status.settings)},
                    {"asset", {{"index", index}, {"unsavedChanges", status.unsavedChanges}}},
                    {"streaming", status.streamingEnabled},
                    {"agents", agents},
                    {"routes", routes},
                    {"worldMode", worldMode}
                };

                std::string text = status.hasNavmesh
                    ? "Navmesh: " + std::to_string(status.stats.tileCount) + " tile(s), " +
                          std::to_string(status.stats.polyCount) + " polygons"
                    : std::string("No navmesh is baked");
                text += status.bakeRunning ? " (a bake is running)" : "";
                text += "; " + std::to_string(agents.size()) + " agent(s), " + std::to_string(routes.size()) +
                        " route(s).";
                if (status.unsavedChanges)
                {
                    text += " The last bake was never saved (navmesh_bake saves what it bakes).";
                }
                return reply(std::move(out), std::move(text));
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // navmesh_clear
        // ------------------------------------------------------------------

        void registerNavmeshClear(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "navmesh_clear";
            tool.title = "Clear navmesh";
            tool.description =
                "Drop the scene's navmesh: agents cannot move until the next navmesh_bake. The files on disk are kept, "
                "but the scene stops referencing them (the scene root's NavmeshComponent is removed): run scene_save "
                "to make that permanent; reloading the scene before that loads the saved navmesh again. Refused in "
                "Play mode, while a scene load is pending and while a bake runs. Not undoable (bake again instead). "
                "Returns {cleared, hadNavmesh, keptFiles}.";
            tool.inputSchema = closedObject(nlohmann::json::object());
            tool.destructive = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {}, "navmesh_clear");
                if (const std::optional<std::string> blocked = env::editBlocker("clear the navmesh"))
                {
                    return ToolResult::error(*blocked);
                }
                auto& dispatcher = events::EventDispatcher::instance();
                const events::navmesh::NavmeshStatus status = dispatcher.query(events::navmesh::GetNavmeshStatusQuery{});
                // A clear waits on the main thread for the bake's worker to finish.
                if (status.bakeRunning)
                {
                    return ToolResult::error("Cannot clear the navmesh while a navmesh bake is running; wait until "
                                             "navmesh_get_info reports bake.state idle, then retry");
                }

                dispatcher.execute(events::navmesh::ClearNavmeshCommand{});

                const std::optional<fs::path> root = tryTerrainProjectRoot();
                const nlohmann::json kept = status.indexPath.empty()
                    ? nlohmann::json(nullptr)
                    : nlohmann::json(projectRelativeUtf8(root, terrainEnginePath(root, status.indexPath)));
                std::string text = status.hasNavmesh ? "Cleared the navmesh; agents cannot move until the next "
                                                       "navmesh_bake."
                                                     : "There was no navmesh; the scene's reference was cleared.";
                if (!kept.is_null())
                {
                    text += " Its files are kept (" + kept.get<std::string>() + "), but the scene no longer references "
                            "them: run scene_save to make that permanent (reloading the scene first loads them again).";
                }
                return reply({{"cleared", true}, {"hadNavmesh", status.hasNavmesh}, {"keptFiles", kept}},
                             std::move(text));
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // navmesh_find_path
        // ------------------------------------------------------------------

        nlohmann::json endpointSchema(std::string description)
        {
            return {
                {"type", nlohmann::json::array({"array", "integer"})},
                {"items", {{"type", "number"}}},
                {"minItems", 3},
                {"maxItems", 3},
                {"minimum", 0},
                {"description", std::move(description)}
            };
        }

        std::optional<float> optionalSize(const ArgReader& reader, const char* name, double min, double max)
        {
            if (!reader.has(name))
            {
                return std::nullopt;
            }
            return env::requireFloat(reader.raw(name), min, max, name);
        }

        nlohmann::json endpointJson(const navtool::Endpoint& endpoint, const navtool::Probe& probe)
        {
            nlohmann::json out{{"given", navtool::pointJson(endpoint.point)}};
            if (endpoint.entity.has_value())
            {
                out["entity"] = toId(endpoint.entity->handle);
                out["name"] = endpoint.entity->name;
                if (endpoint.entity->hasComponent(services::ComponentTypeId::NavmeshAgent))
                {
                    out["baseOffset"] = env::floatJson(endpoint.baseOffset);
                }
            }
            out.update(navtool::probeJson(probe, endpoint.point));
            return out;
        }

        std::string offNavmeshReason(const char* which, float snapRadius)
        {
            return std::string("the ") + which + " is off the navmesh: nothing walkable lies within snapRadius " +
                   navtool::numberText(snapRadius) + " m of it (move it, raise snapRadius up to 20, or bake a navmesh "
                   "that covers it)";
        }

        void registerNavmeshFindPath(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "navmesh_find_path";
            tool.title = "Find navmesh path";
            tool.description =
                "Find the walkable path between two places on the navmesh, as a NavmeshAgent would walk it, and say "
                "why there is none. start / end: [x, y, z] or an entity id (its world position; for a NavmeshAgent its "
                "feet, the world position minus baseOffset). Each end snaps to the nearest navmesh point within "
                "snapRadius (default 5 m, 0.5-20). agentRadius / agentHeight default to the navmesh's bake values. "
                "found = a complete path; partial = the end cannot be reached and the path stops at the reachable point "
                "closest to it; reason says why there is no complete path (no navmesh, an end off the navmesh, no "
                "connection, partial). Works in any mode. Returns {found, partial, reason, path [[x, y, z], ...], "
                "length, start {given, entity, name, baseOffset, onNavmesh, nearest, offset}, end {...}, warnings}.";
            tool.inputSchema = closedObject({
                {"start", endpointSchema("Where the path starts: [x, y, z] in world metres, or an entity id.")},
                {"end", endpointSchema("Where the path ends: [x, y, z] in world metres, or an entity id.")},
                {"agentRadius", schema::numberRange("Metres. Default: the navmesh's bake agentRadius.", 0.1, 5.0)},
                {"agentHeight", schema::numberRange("Metres. Default: the navmesh's bake agentHeight.", 0.5, 10.0)},
                {"snapRadius", schema::numberRange("Metres each end may move onto the navmesh. Default 5.",
                                                   minSnapRadius, maxSnapRadius)}
            }, {"start", "end"});
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"start", "end", "agentRadius", "agentHeight", "snapRadius"},
                                       "navmesh_find_path");
                const ArgReader reader(args);
                const navtool::Endpoint start = navtool::requireEndpoint(reader.raw("start"), "start");
                const navtool::Endpoint end = navtool::requireEndpoint(reader.raw("end"), "end");
                const std::optional<float> radius = optionalSize(reader, "agentRadius", 0.1, 5.0);
                const std::optional<float> height = optionalSize(reader, "agentHeight", 0.5, 10.0);
                const float snapRadius = reader.has("snapRadius")
                    ? env::requireFloat(reader.raw("snapRadius"), minSnapRadius, maxSnapRadius, "snapRadius")
                    : static_cast<float>(defaultSnapRadius);

                nlohmann::json out{
                    {"found", false},
                    {"partial", false},
                    {"reason", nullptr},
                    {"path", nlohmann::json::array()},
                    {"length", 0}
                };
                std::vector<std::string> warnings;

                if (!navtool::hasNavmesh())
                {
                    const std::string reason = "there is no navmesh: bake one with navmesh_bake";
                    out["reason"] = reason;
                    out["start"] = endpointJson(start, navtool::Probe{});
                    out["end"] = endpointJson(end, navtool::Probe{});
                    out["warnings"] = warnings;
                    return reply(std::move(out), "No path: " + reason + ".");
                }

                auto& dispatcher = events::EventDispatcher::instance();
                const events::navmesh::NavmeshStatus status = dispatcher.query(events::navmesh::GetNavmeshStatusQuery{});
                const navtool::Probe startProbe = navtool::probe(start.point, snapRadius);
                const navtool::Probe endProbe = navtool::probe(end.point, snapRadius);
                out["start"] = endpointJson(start, startProbe);
                out["end"] = endpointJson(end, endProbe);

                std::vector<std::string> reasons;
                if (!startProbe.nearest.has_value())
                {
                    reasons.push_back(offNavmeshReason("start", snapRadius));
                }
                if (!endProbe.nearest.has_value())
                {
                    reasons.push_back(offNavmeshReason("end", snapRadius));
                }
                if (!reasons.empty())
                {
                    const std::string reason = reasons.size() == 2 ? reasons[0] + "; " + reasons[1] : reasons[0];
                    out["reason"] = reason;
                    out["warnings"] = warnings;
                    return reply(std::move(out), "No path: " + reason + ".");
                }

                const auto warnSnapped = [&warnings](const char* which, const glm::vec3& given, const glm::vec3& nearest)
                {
                    const float offset = glm::distance(given, nearest);
                    if (offset > snapWarningDistance)
                    {
                        warnings.push_back(std::string("The ") + which + " was " + navtool::numberText(offset) +
                                           " m off the navmesh; the path uses the nearest navmesh point " +
                                           navtool::pointJson(nearest).dump() + ".");
                    }
                };
                warnSnapped("start", start.point, *startProbe.nearest);
                warnSnapped("end", end.point, *endProbe.nearest);

                events::navmesh::FindPathQuery query;
                query.start = *startProbe.nearest;
                query.end = *endProbe.nearest;
                query.agentRadius = radius.value_or(status.settings.agentRadius);
                query.agentHeight = height.value_or(status.settings.agentHeight);
                const navigation::NavPath path = dispatcher.query(query);
                if (!path.isValid || path.waypoints.empty())
                {
                    const std::string reason =
                        "no path connects them: the start and the end lie on parts of the navmesh that do not connect "
                        "(a wall, a gap, a step above stepHeight or a slope above maxSlope separates them)";
                    out["reason"] = reason;
                    out["warnings"] = warnings;
                    return reply(std::move(out), "No path: " + reason + ".");
                }

                nlohmann::json points = nlohmann::json::array();
                for (const glm::vec3& waypoint : path.waypoints)
                {
                    points.push_back(navtool::pointJson(waypoint));
                }
                const float length = navtool::pathLength(path.waypoints);
                out["path"] = std::move(points);
                out["length"] = env::floatJson(length);

                // Detour's isPartial, plus the ends: a hierarchical path never sets isPartial.
                const float startGap = glm::distance(path.waypoints.front(), *startProbe.nearest);
                const float endGap = glm::distance(path.waypoints.back(), *endProbe.nearest);
                if (path.isPartial || endGap > endpointTolerance || startGap > endpointTolerance)
                {
                    std::string reason = "partial: the end cannot be reached from the start";
                    reason += endGap > endpointTolerance
                        ? "; the path stops " + navtool::numberText(endGap) + " m short of it, at " +
                              navtool::pointJson(path.waypoints.back()).dump()
                        : std::string("; the path stops at the reachable point closest to it");
                    if (startGap > endpointTolerance)
                    {
                        reason += "; the path also begins " + navtool::numberText(startGap) +
                                  " m from the start (part of the way cannot be walked)";
                    }
                    out["partial"] = true;
                    out["reason"] = reason;
                    out["warnings"] = warnings;
                    return reply(std::move(out), "Partial path (" + std::to_string(path.waypoints.size()) +
                                                     " waypoints): " + reason + ".");
                }

                out["found"] = true;
                out["warnings"] = warnings;
                return reply(std::move(out), "Found a path: " + std::to_string(path.waypoints.size()) + " waypoints, " +
                                                 navtool::numberText(length) + " m.");
            };
            registry.add(std::move(tool));
        }
    }

    void registerNavigationTools(ToolRegistry& registry, const ToolContext& context)
    {
        registerNavmeshBake(registry, context);
        registerNavmeshGetInfo(registry);
        registerNavmeshClear(registry);
        registerNavmeshFindPath(registry);
        navtool::registerRouteTools(registry, context);
    }
}
