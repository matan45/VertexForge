#include "CoreTools.hpp"
#include "EnvironmentToolSupport.hpp"
#include "SettingsTable.hpp"
#include "TerrainToolSupport.hpp"
#include "ToolHelpers.hpp"
#include "VolumetricToolSupport.hpp"
#include "../protocol/ArgReader.hpp"

#include "data/DTOs.hpp"
#include "data/VolumetricNavData.hpp"
#include "events/EventDispatcher.hpp"
#include "events/render/DebugDrawEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/volumetric/VolumetricNavEvents.hpp"
#include "navigation/volumetric/VolumetricPathfinder.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// VK-1660 volumetric navigation tools: volumetric_bake, volumetric_get_info, volumetric_find_path,
// volumetric_clear. A VolumetricNavVolume is the airspace flying agents (VolumetricAgent) path through: the bake
// voxelizes its box in the background (events::volumetric ticket + outcome) and SaveVolumetricVolumeCommand writes
// the grid to the .vfVolNav its bakedRef then points at, which the scene keeps once saved. The volume's settings
// are component fields (component_set VolumetricNavVolume, VolumetricBindings.cpp); none of these tools is
// undoable. Shared tables, names and warnings live in VolumetricToolSupport.
namespace mcp::tools
{
    namespace
    {
        // volumetric_bake polls every bake of one call until this deadline, then cancels what still runs.
        constexpr std::chrono::milliseconds bakeDeadline{600000};
        // The deadline plus room for the hops after it (a Worker tool keeps its own time; see bakeDeadline).
        constexpr std::chrono::milliseconds bakeTimeout{630000};
        constexpr std::chrono::milliseconds pollInterval{100};

        constexpr double maxSnapRadius = 10.0; // metres
        constexpr double defaultSnapRadius = 2.0;
        // volumetric_find_path show: how long the path stays drawn (as eqs_run's showSeconds).
        constexpr double minShowSeconds = 1.0;
        constexpr double maxShowSeconds = 60.0;
        constexpr double defaultShowSeconds = 10.0;
        constexpr double maxCoordinate = 1.0e6; // metres, the bound of the terrain tools' ground points

        constexpr const char* worldModeRefusal =
            "Cannot bake volumetric navigation in World mode: the volumes there belong to the world's streaming "
            "sectors. Bake them in an ordinary scene.";

        constexpr const char* saveNote =
            "Each grid is saved to its .vfVolNav and the volume's bakedRef points at it: run scene_save so the saved "
            "scene references the files (a reload before that forgets the new bakes).";

        constexpr const char* clearNote =
            "Not undoable. The grids are unloaded and each bakedRef cleared; the .vfVolNav files stay on disk. Run "
            "scene_save so the saved scene stops referencing them: reloading the scene before that loads them again.";

        std::string numberText(float value)
        {
            return env::floatJson(value).dump();
        }

        std::string countText(std::size_t count, const char* noun)
        {
            return std::to_string(count) + " " + noun + (count == 1 ? "" : "s");
        }

        // VolumetricNavServiceImpl's percentage.
        float navigablePercent(int64_t navigable, int64_t voxels)
        {
            return voxels > 0 ? 100.0f * static_cast<float>(navigable) / static_cast<float>(voxels) : 0.0f;
        }

        // ------------------------------------------------------------------
        // volumetric_bake
        // ------------------------------------------------------------------

        struct BakeJob
        {
            services::EntityHandle volume;
            std::string name;
            float agentClearance = 0.0f;
            uint64_t ticket = 0;
            bool done = false;                     // the bake left Running
            services::VolumetricBakeOutcome outcome; // the latest poll
            services::VolumetricSaveResult saved;  // after a Succeeded outcome
            std::string file;                      // the saved grid as the tools report files
        };

        struct BakeStart
        {
            std::string error; // a refusal: nothing was started
            std::vector<BakeJob> jobs;
        };

        std::string jobLabel(const BakeJob& job)
        {
            return "volume " + std::to_string(toId(job.volume)) + " ('" + job.name + "')";
        }

        // Main thread.
        void cancelBakes(const std::vector<services::EntityHandle>& volumes)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            for (const services::EntityHandle& volume : volumes)
            {
                events::volumetric::CancelVolumetricBakeCommand command;
                command.volume = volume;
                dispatcher.execute(command);
            }
        }

        // Main thread. Every target is checked before the first bake starts, and a refused request cancels the
        // bakes started before it: the call bakes all of them or none.
        BakeStart startBakes(const std::optional<uint32_t>& volumeId)
        {
            BakeStart start;
            if (const std::optional<std::string> blocked = env::editBlocker("bake volumetric navigation"))
            {
                start.error = *blocked;
                return start;
            }
            if (isWorldModeActive())
            {
                start.error = worldModeRefusal;
                return start;
            }

            const std::vector<services::VolumetricVolumeStatus> volumes = volumetrictool::listVolumes();
            std::vector<services::VolumetricVolumeStatus> targets;
            if (volumeId.has_value())
            {
                targets.push_back(volumetrictool::requireVolume(volumes, *volumeId, "volume"));
            }
            else
            {
                targets = volumes;
            }
            if (targets.empty())
            {
                start.error = "The scene has no VolumetricNavVolume to bake: component_add VolumetricNavVolume to an "
                              "entity first (its box is centred on the entity's world position).";
                return start;
            }

            for (const services::VolumetricVolumeStatus& target : targets)
            {
                if (target.bakeState == services::VolumetricBakeState::Running)
                {
                    start.error = std::format("A bake of {} is already running ({:.0f}%); call volumetric_bake again once "
                                              "it has finished (volumetric_get_info shows its progress).",
                                              volumetrictool::volumeLabel(target), target.progress * 100.0f);
                    return start;
                }
                const std::string cap = volumetrictool::voxelCapProblem(target.worldMin, target.worldMax, target.voxelSize);
                if (!cap.empty())
                {
                    start.error = "Cannot bake " + volumetrictool::volumeLabel(target) + ": " + cap +
                                  " (component_set VolumetricNavVolume). Nothing was baked.";
                    return start;
                }
            }

            auto& dispatcher = events::EventDispatcher::instance();
            for (const services::VolumetricVolumeStatus& target : targets)
            {
                events::volumetric::RequestVolumetricBakeCommand request;
                request.volume = target.entity;
                const services::VolumetricBakeTicket ticket = dispatcher.execute(request);
                if (!ticket.accepted)
                {
                    std::vector<services::EntityHandle> started;
                    for (const BakeJob& job : start.jobs)
                    {
                        started.push_back(job.volume);
                    }
                    cancelBakes(started);
                    start.error = "The bake of " + volumetrictool::volumeLabel(target) + " was refused: " +
                                  (ticket.error.empty() ? std::string("no reason given") : ticket.error) +
                                  (started.empty() ? std::string() : "; the bakes this call had started were cancelled");
                    start.jobs.clear();
                    return start;
                }

                BakeJob job;
                job.volume = target.entity;
                job.name = target.name;
                job.agentClearance = target.agentClearance;
                job.ticket = ticket.ticket;
                start.jobs.push_back(std::move(job));
            }
            return start;
        }

        // Main thread. Reads the outcome of every unfinished bake and saves each one that succeeded at once, into
        // the folder of the volume's current file, else volnav/<scene name> in the project's assets folder.
        void pollBakes(std::vector<BakeJob>& jobs)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            for (BakeJob& job : jobs)
            {
                if (job.done)
                {
                    continue;
                }
                events::volumetric::GetVolumetricBakeOutcomeQuery query;
                query.ticket = job.ticket;
                job.outcome = dispatcher.query(query);
                if (job.outcome.state == services::VolumetricBakeState::Running)
                {
                    continue;
                }
                job.done = true;
                if (job.outcome.state != services::VolumetricBakeState::Succeeded)
                {
                    continue;
                }

                events::volumetric::SaveVolumetricVolumeCommand save;
                save.volume = job.volume;
                job.saved = dispatcher.execute(save);
                if (job.saved.success)
                {
                    job.file = volumetrictool::reportedFile(job.saved.path);
                }
            }
        }

        // Why a finished bake left no saved grid; "" when it baked and saved.
        std::string bakeFailure(const BakeJob& job)
        {
            switch (job.outcome.state)
            {
            case services::VolumetricBakeState::Succeeded:
                if (job.saved.success)
                {
                    return {};
                }
                return "baked, but NOT saved: " +
                       (job.saved.error.empty() ? std::string("no reason given") : job.saved.error) +
                       " (the grid stays loaded until the scene reloads)";
            case services::VolumetricBakeState::Failed:
                return "the bake failed: " +
                       (job.outcome.message.empty() ? std::string("no reason given") : job.outcome.message);
            case services::VolumetricBakeState::Cancelled:
                return "the bake was cancelled (by volumetric_clear, a scene change or the editor)";
            case services::VolumetricBakeState::Running:
                return "the bake is still running";
            case services::VolumetricBakeState::Unknown:
                break;
            }
            return "the editor has no result for the bake (ticket " + std::to_string(job.ticket) +
                   "); call volumetric_bake again";
        }

        // "volume 3 ('Sky') 60x30x60, 75.0% navigable -> volnav/Level/Sky_<uuid>.vfVolNav, ..." over the
        // jobs that baked and saved.
        std::string savedList(const std::vector<BakeJob>& jobs)
        {
            std::string list;
            for (const BakeJob& job : jobs)
            {
                if (!job.done || !bakeFailure(job).empty())
                {
                    continue;
                }
                list += list.empty() ? "" : ", ";
                list += std::format("{} {}x{}x{}, {:.1f}% navigable -> {}", jobLabel(job), job.outcome.dims.x,
                                    job.outcome.dims.y, job.outcome.dims.z,
                                    navigablePercent(job.outcome.navigable, job.outcome.voxels), job.file);
            }
            return list;
        }

        // The finished jobs that did not bake and save: "volume 3 ('Sky'): why; ...".
        std::string failureList(const std::vector<BakeJob>& jobs, std::size_t& count)
        {
            std::string list;
            count = 0;
            for (const BakeJob& job : jobs)
            {
                if (!job.done)
                {
                    continue;
                }
                const std::string failure = bakeFailure(job);
                if (failure.empty())
                {
                    continue;
                }
                list += list.empty() ? "" : "; ";
                list += jobLabel(job) + ": " + failure;
                ++count;
            }
            return list;
        }

        nlohmann::json bakedJson(const BakeJob& job)
        {
            const services::VolumetricBakeOutcome& outcome = job.outcome;
            std::vector<std::string> warnings;
            if (outcome.voxels > 0 && outcome.navigable == 0)
            {
                warnings.push_back("no voxel is navigable: the box lies inside geometry, or agentClearance " +
                                   numberText(job.agentClearance) + " m leaves no free space, so nothing can fly here");
            }
            else if (outcome.voxels > 0 && outcome.navigable == outcome.voxels)
            {
                warnings.emplace_back("every voxel is navigable: the bake found no geometry in the box (includeTerrain, "
                                      "includeStaticMeshes and includeColliders choose what it treats as solid)");
            }

            nlohmann::json out = nlohmann::json::object();
            out["volume"] = entityId(job.volume);
            out["name"] = job.name;
            out["dims"] = volumetrictool::ivec3Json(outcome.dims);
            out["voxels"] = outcome.voxels;
            out["navigablePercent"] = env::floatJson(navigablePercent(outcome.navigable, outcome.voxels));
            out["file"] = job.file;
            out["durationMs"] = static_cast<int64_t>(std::llround(outcome.durationMs));
            out["warnings"] = warnings;
            return out;
        }

        ToolResult bakeReply(const std::vector<BakeJob>& jobs)
        {
            std::size_t failed = 0;
            const std::string failures = failureList(jobs, failed);
            const std::string saved = savedList(jobs);
            if (failed > 0)
            {
                std::string message = std::to_string(failed) + " of " + countText(jobs.size(), "volume") +
                                      " did not bake: " + failures + ".";
                if (!saved.empty())
                {
                    message += " Baked and saved: " + saved + "; run scene_save so the scene keeps their references.";
                }
                return ToolResult::error(std::move(message));
            }

            nlohmann::json volumes = nlohmann::json::array();
            for (const BakeJob& job : jobs)
            {
                volumes.push_back(bakedJson(job));
            }
            nlohmann::json structured = nlohmann::json::object();
            structured["volumes"] = std::move(volumes);
            structured["note"] = saveNote;
            return reply(std::move(structured), "Baked and saved " + countText(jobs.size(), "volume") + ": " + saved +
                                                    ". Run scene_save so the scene keeps the references.");
        }

        // The deadline passed: the bakes still running are cancelled, the finished ones reported.
        ToolResult bakeTimedOut(const ToolContext& context, const std::vector<BakeJob>& jobs)
        {
            std::vector<services::EntityHandle> running;
            std::string unfinished;
            for (const BakeJob& job : jobs)
            {
                if (job.done)
                {
                    continue;
                }
                running.push_back(job.volume);
                unfinished += unfinished.empty() ? "" : ", ";
                unfinished += std::format("{} at {:.0f}%", jobLabel(job), job.outcome.progress * 100.0f);
            }
            context.runOnMain([running]() -> nlohmann::json
            {
                cancelBakes(running);
                return nullptr;
            });

            std::string message = "volumetric_bake did not finish within " + std::to_string(bakeDeadline.count() / 1000) +
                                  " s and cancelled " + unfinished + ": bake a smaller box or a larger voxelSize.";
            std::size_t failed = 0;
            const std::string failures = failureList(jobs, failed);
            if (failed > 0)
            {
                message += " Did not bake: " + failures + ".";
            }
            const std::string saved = savedList(jobs);
            if (!saved.empty())
            {
                message += " Baked and saved: " + saved + "; run scene_save so the scene keeps their references.";
            }
            return ToolResult::error(std::move(message));
        }

        void registerVolumetricBake(ToolRegistry& registry, const ToolContext& context)
        {
            ToolDef tool;
            tool.name = "volumetric_bake";
            tool.title = "Bake volumetric navigation";
            tool.description =
                "Bake the navigation grid of VolumetricNavVolume entities, the airspace flying agents (VolumetricAgent) "
                "path through, and save each grid to the .vfVolNav file its volume's bakedRef then points at. Without "
                "'volume' every volume of the scene is baked. A volume is the axis-aligned box centred on its entity's "
                "WORLD position + centerOffset, +/- halfExtents (rotation and scale are ignored). voxelSize, "
                "connectivity (6 | 26), agentClearance (the free space kept around geometry: at least the largest "
                "radius of the agents that fly there) and the include flags (terrain, static meshes and foliage, "
                "colliders) are component fields: set them with component_set VolumetricNavVolume first. A grid holds "
                "at most 16777216 voxels (2 * halfExtents / voxelSize per axis, multiplied). The bake runs in the "
                "background (seconds; minutes for the largest grids) while the editor keeps rendering. Each grid is "
                "written to <the folder of the volume's current file, else volnav/<scene name>>/<volume "
                "name>_<uuid>.vfVolNav: run scene_save afterwards so the scene keeps the reference. Not undoable. "
                "Refused in Play mode, while a scene load is pending, in World mode, when the scene has no volume, when "
                "a volume needs more voxels than the limit (the error names the smallest voxelSize that fits) and while "
                "one of the volumes is already baking. Returns {volumes: [{volume, name, dims, voxels, "
                "navigablePercent, file, durationMs, warnings}], note}.";
            tool.inputSchema = closedObject({
                {"volume", schema::entity("VolumetricNavVolume entity to bake. Default: every volume of the scene.")}
            });
            // Worker: the bakes run for seconds on background jobs; only the start, poll and save hops touch the
            // main thread, so the editor keeps rendering meanwhile.
            tool.affinity = ThreadAffinity::Worker;
            tool.destructive = true;
            tool.timeout = bakeTimeout;
            tool.handler = [context](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"volume"}, "volumetric_bake");
                const ArgReader reader(args);
                const std::optional<uint32_t> volumeId = reader.optEntity("volume");

                // The hops share their results through slots captured by value, so a hop that runs after its
                // caller timed out writes into a slot nobody reads any more.
                const auto start = std::make_shared<BakeStart>();
                context.runOnMain([start, volumeId]() -> nlohmann::json
                {
                    *start = startBakes(volumeId);
                    return nullptr;
                });
                if (!start->error.empty())
                {
                    return ToolResult::error(start->error);
                }

                std::vector<BakeJob> jobs = start->jobs;
                const auto deadline = std::chrono::steady_clock::now() + bakeDeadline;
                while (true)
                {
                    const auto slot = std::make_shared<std::vector<BakeJob>>(jobs);
                    context.runOnMain([slot]() -> nlohmann::json
                    {
                        pollBakes(*slot);
                        return nullptr;
                    });
                    jobs = *slot;
                    if (std::all_of(jobs.begin(), jobs.end(), [](const BakeJob& job) { return job.done; }))
                    {
                        return bakeReply(jobs);
                    }
                    if (std::chrono::steady_clock::now() >= deadline)
                    {
                        return bakeTimedOut(context, jobs);
                    }
                    std::this_thread::sleep_for(pollInterval);
                }
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // volumetric_get_info
        // ------------------------------------------------------------------

        nlohmann::json volumeJson(const services::VolumetricVolumeStatus& volume)
        {
            nlohmann::json bake = nlohmann::json::object();
            bake["state"] = std::string(volumetrictool::bakeStateName(volume.bakeState));
            bake["progress"] = env::floatJson(volume.progress);

            nlohmann::json out = nlohmann::json::object();
            out["entity"] = entityId(volume.entity);
            out["name"] = volume.name;
            out["worldBounds"] = volumetrictool::boundsJson(volume.worldMin, volume.worldMax);
            out["voxelSize"] = env::floatJson(volume.voxelSize);
            out["connectivity"] = volume.connectivity;
            out["clearance"] = env::floatJson(volume.agentClearance);
            out["baked"] = volume.baked;
            out["stale"] = volume.stale;
            out["dims"] = volumetrictool::ivec3Json(volume.dims);
            out["navigablePercent"] = env::floatJson(volume.navigablePercent);
            out["file"] = volumetrictool::reportedFile(volume.file);
            out["bake"] = std::move(bake);
            out["warnings"] = volumetrictool::volumeWarnings(volume);
            return out;
        }

        // nullopt when the entity lost its VolumetricAgent between the hierarchy and the data query.
        std::optional<nlohmann::json> agentJson(const services::EntityData& entity,
                                                const std::vector<services::VolumetricVolumeStatus>& volumes)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::volumetric::GetVolumetricAgentDataQuery dataQuery;
            dataQuery.entity = entity.handle;
            const std::optional<services::VolumetricAgentData> data = dispatcher.query(dataQuery);
            if (!data.has_value())
            {
                return std::nullopt;
            }
            events::volumetric::GetVolumetricAgentStateQuery stateQuery;
            stateQuery.entity = entity.handle;
            const std::optional<services::VolumetricAgentState> state = dispatcher.query(stateQuery);
            const services::VolumetricAgentMoveState moveState =
                state.has_value() ? state->state : services::VolumetricAgentMoveState::Idle;
            const services::VolumetricVolumeStatus* volume =
                volumetrictool::bakedVolumeAt(volumes, entity.worldTransform.position);

            nlohmann::json out = nlohmann::json::object();
            out["entity"] = entityId(entity.handle);
            out["name"] = entity.name;
            out["position"] = env::vec3Json(entity.worldTransform.position);
            out["registered"] = data->registered;
            out["state"] = std::string(volumetrictool::moveStateName(moveState));
            if (moveState == services::VolumetricAgentMoveState::Failed)
            {
                out["failReason"] = state->failReason;
            }
            out["inVolume"] = volume != nullptr ? entityId(volume->entity) : nlohmann::json(nullptr);
            out["warnings"] = volumetrictool::agentWarnings(entity, data->radius, volumes);
            return out;
        }

        void registerVolumetricGetInfo(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "volumetric_get_info";
            tool.title = "Get volumetric navigation";
            tool.description =
                "Read the scene's volumetric navigation (flying agents). volumes: [{entity, name, worldBounds {min, max} "
                "(the box now: the entity's world position + centerOffset +/- halfExtents), voxelSize, connectivity, "
                "clearance (agentClearance), baked (a grid is loaded), stale (the box or settings changed since that "
                "grid was baked: bake again), dims (of the loaded grid), navigablePercent (0-100), file (the saved "
                ".vfVolNav, \"\" = never saved), bake {state none | running | succeeded | failed | cancelled (its "
                "latest bake this session), progress 0-1}, warnings}]. agents: the VolumetricAgent entities [{entity, "
                "name, position, registered (the agent manager drives it: Play only), state idle | moving | arrived | "
                "failed, failReason (when failed), inVolume (the baked volume it flies in, null = none), warnings}]. "
                "'volume' limits both to that volume and the agents inside its box. Works in any mode.";
            tool.inputSchema = closedObject({
                {"volume", schema::entity("Only this VolumetricNavVolume and the agents inside its box. Default: all.")}
            });
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"volume"}, "volumetric_get_info");
                const ArgReader reader(args);
                const std::optional<uint32_t> volumeId = reader.optEntity("volume");

                const std::vector<services::VolumetricVolumeStatus> volumes = volumetrictool::listVolumes();
                const services::VolumetricVolumeStatus* only =
                    volumeId.has_value() ? &volumetrictool::requireVolume(volumes, *volumeId, "volume") : nullptr;

                nlohmann::json volumeList = nlohmann::json::array();
                std::size_t baked = 0;
                std::size_t stale = 0;
                for (const services::VolumetricVolumeStatus& volume : volumes)
                {
                    if (only != nullptr && volume.entity != only->entity)
                    {
                        continue;
                    }
                    volumeList.push_back(volumeJson(volume));
                    baked += volume.baked ? 1 : 0;
                    stale += volume.baked && volume.stale ? 1 : 0;
                }

                nlohmann::json agentList = nlohmann::json::array();
                const services::SceneHierarchyData hierarchy =
                    events::EventDispatcher::instance().query(events::scene::GetSceneHierarchyQuery{});
                for (const services::EntityData& entity : hierarchy.entities)
                {
                    if (!entity.hasComponent(services::ComponentTypeId::VolumetricAgent))
                    {
                        continue;
                    }
                    if (only != nullptr && !volumetrictool::boxContains(*only, entity.worldTransform.position))
                    {
                        continue;
                    }
                    if (std::optional<nlohmann::json> agent = agentJson(entity, volumes))
                    {
                        agentList.push_back(std::move(*agent));
                    }
                }

                const std::string text = countText(volumeList.size(), "volume") + " (" + std::to_string(baked) +
                                         " baked, " + std::to_string(stale) + " stale), " +
                                         countText(agentList.size(), "flying agent") + ".";
                nlohmann::json structured = nlohmann::json::object();
                structured["volumes"] = std::move(volumeList);
                structured["agents"] = std::move(agentList);
                return reply(std::move(structured), text);
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // volumetric_find_path
        // ------------------------------------------------------------------

        struct Endpoint
        {
            glm::vec3 point{0.0f};
            std::optional<uint32_t> entity; // set when the argument named an entity
        };

        // An entity id (its world position) or a world point [x, y, z].
        Endpoint readEndpoint(const ArgReader& reader, const char* name)
        {
            const nlohmann::json& value = reader.raw(name);
            if (value.is_number())
            {
                const services::EntityData entity = requireEntity(reader, name);
                return Endpoint{entity.worldTransform.position, toId(entity.handle)};
            }
            if (!value.is_array())
            {
                throw ArgError(std::string("argument '") + name + "' must be an entity id or a world point [x, y, z]");
            }
            return Endpoint{env::requireVec3(value, -maxCoordinate, maxCoordinate, name), std::nullopt};
        }

        nlohmann::json endpointSchema(std::string description)
        {
            return {
                {"type", nlohmann::json::array({"integer", "array"})},
                {"minimum", 0},
                {"items", {{"type", "number"}}},
                {"minItems", 3},
                {"maxItems", 3},
                {"description", std::move(description)}
            };
        }

        bool navigableAt(const glm::vec3& point)
        {
            events::volumetric::IsPointNavigable3DQuery query;
            query.point = point;
            return events::EventDispatcher::instance().query(query);
        }

        // `pathPoint` = the path's point for this end (its first or last waypoint); nullopt without a path.
        nlohmann::json endpointJson(const Endpoint& endpoint, bool navigable, const std::optional<glm::vec3>& snapped,
                                    const std::optional<glm::vec3>& pathPoint)
        {
            nlohmann::json out = nlohmann::json::object();
            out["given"] = env::vec3Json(endpoint.point);
            if (endpoint.entity.has_value())
            {
                out["entity"] = *endpoint.entity;
            }
            out["navigable"] = navigable;
            if (snapped.has_value())
            {
                out["snapped"] = env::vec3Json(*snapped);
            }
            out["offset"] = pathPoint.has_value() ? env::floatJson(glm::distance(endpoint.point, *pathPoint))
                                                  : nlohmann::json(nullptr);
            return out;
        }

        float pathLength(const std::vector<glm::vec3>& path)
        {
            float length = 0.0f;
            for (std::size_t i = 1; i < path.size(); ++i)
            {
                length += glm::distance(path[i - 1], path[i]);
            }
            return length;
        }

        // Why the search did not reach the end; "" for Found. `startOutside` / `endOutside`: no baked volume's
        // box holds that point. `shortBy`: Partial, how far the path's last point is from the end.
        std::string pathReason(::volumetric::VolumePathStatus status, float snapRadius, bool startOutside,
                               bool endOutside, float shortBy)
        {
            const std::string blocked = " lies in blocked space (inside geometry, or closer to it than the volume's "
                                        "agentClearance) and no navigable voxel is within snapRadius " +
                                        numberText(snapRadius) + " m: move it, or raise snapRadius (up to 10)";
            switch (status)
            {
            case ::volumetric::VolumePathStatus::Found:
                return {};
            case ::volumetric::VolumePathStatus::Partial:
                return "the end cannot be reached from the start (no navigable connection, or the search stopped at "
                       "its node limit): the path ends at the reachable point nearest to the end, " +
                       numberText(shortBy) + " m short of it";
            case ::volumetric::VolumePathStatus::NoVolume:
                return "no VolumetricNavVolume is baked: volumetric_bake one (volumetric_get_info lists the volumes)";
            case ::volumetric::VolumePathStatus::OutsideVolume:
            {
                const char* which = startOutside && endOutside ? "both the start and the end lie"
                                    : startOutside             ? "the start lies"
                                    : endOutside               ? "the end lies"
                                                               : "the start or the end lies";
                return std::string(which) + " outside every baked volume: move the point into a baked volume's box, "
                                            "or grow the volume (volumetric_get_info lists the boxes)";
            }
            case ::volumetric::VolumePathStatus::StartBlocked:
                return "the start" + blocked;
            case ::volumetric::VolumePathStatus::EndBlocked:
                return "the end" + blocked;
            case ::volumetric::VolumePathStatus::DifferentVolumes:
                return "the start and the end lie in different baked volumes and no single volume holds both: grow one "
                       "volume to cover both points, or fly in two legs";
            case ::volumetric::VolumePathStatus::NoPath:
                return "not one step toward the end is possible: the navigable space around the start is enclosed";
            }
            return "the search ended with an unknown status";
        }

        // Draws the path into the debug overlay for `seconds` (ShowVolumetricPathCommand: the volumetric service
        // redraws it every frame, replacing the previous one). Returns whether anything was drawn; `warnings` says
        // why not.
        bool showPath(const ::volumetric::VolumePathResult& result, const Endpoint& start, const Endpoint& end,
                      bool hasPath, float seconds, std::vector<std::string>& warnings)
        {
            if (!hasPath)
            {
                warnings.emplace_back("show: there is no path to draw");
                return false;
            }
            auto& dispatcher = events::EventDispatcher::instance();
            try
            {
                if (!dispatcher.query(events::debugdraw::GetDebugDrawEnabledQuery{}))
                {
                    warnings.emplace_back("show: debug drawing is disabled in the editor, so nothing was drawn");
                    return false;
                }
                events::volumetric::ShowVolumetricPathCommand command;
                command.waypoints = result.waypoints;
                if (result.startSnapped.has_value())
                {
                    command.snaps.emplace_back(start.point, *result.startSnapped);
                }
                if (result.endSnapped.has_value())
                {
                    command.snaps.emplace_back(end.point, *result.endSnapped);
                }
                command.seconds = seconds;
                dispatcher.execute(command);
            }
            catch (const std::runtime_error&)
            {
                warnings.emplace_back("show: this editor has no debug-draw or volumetric navigation service, so "
                                      "nothing was drawn");
                return false;
            }
            return true;
        }

        void registerVolumetricFindPath(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "volumetric_find_path";
            tool.title = "Find flight path";
            tool.description =
                "Find a flight path through the baked VolumetricNavVolumes, as a VolumetricAgent would fly it. start / "
                "end: an entity id (its world position) or a world point [x, y, z] in metres (Y up). A point in "
                "blocked space (inside geometry, or closer to it than the volume's agentClearance) moves to the nearest "
                "navigable voxel centre within snapRadius (0-10 m, default 2). Both points must lie in one baked volume "
                "(the one with the fewest voxels when several hold them). Returns {found, partial (the end is not "
                "reachable: the path stops at the reachable point nearest to it), status (Found | Partial | NoVolume | "
                "OutsideVolume | StartBlocked | EndBlocked | DifferentVolumes | NoPath), reason (unless found), volume "
                "(entity id, null when none was searched), path [[x, y, z], ...] (smoothed waypoints), length (m), "
                "start {given, entity (when given as one), navigable, snapped (where the search moved it), offset (m "
                "from given to the path's first point, null without a path)}, end {... offset: from given to the "
                "path's last point}, nodesExpanded, warnings}. show: true also draws the path (magenta, snaps yellow) "
                "into the viewport's debug overlay for showSeconds (viewport_screenshot captures it; a later show "
                "replaces it). Works in any mode.";
            tool.inputSchema = closedObject({
                {"start", endpointSchema("Entity id (its world position) or world point [x, y, z] the flight starts at.")},
                {"end", endpointSchema("Entity id (its world position) or world point [x, y, z] to fly to.")},
                {"snapRadius", schema::numberRange("Metres a blocked start / end may move onto a navigable voxel (0-10). "
                                                   "Default 2.", 0.0, maxSnapRadius)},
                {"show", schema::boolean("Draw the path into the debug overlay for showSeconds. Default false.")},
                {"showSeconds", schema::numberRange("How long the drawing stays, in seconds (1-60). Default 10.",
                                                    minShowSeconds, maxShowSeconds)}
            }, {"start", "end"});
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"start", "end", "snapRadius", "show", "showSeconds"},
                                       "volumetric_find_path");
                const ArgReader reader(args);
                const Endpoint start = readEndpoint(reader, "start");
                const Endpoint end = readEndpoint(reader, "end");
                const float snapRadius =
                    static_cast<float>(reader.optNumberInRange("snapRadius", 0.0, maxSnapRadius, defaultSnapRadius));
                const bool show = reader.optBool("show", false);
                const float showSeconds = static_cast<float>(
                    reader.optNumberInRange("showSeconds", minShowSeconds, maxShowSeconds, defaultShowSeconds));

                events::volumetric::FindPath3DQuery query;
                query.start = start.point;
                query.end = end.point;
                query.snapRadius = snapRadius;
                const ::volumetric::VolumePathResult result = events::EventDispatcher::instance().query(query);
                const bool hasPath = result.hasPath() && !result.waypoints.empty();

                const std::vector<services::VolumetricVolumeStatus> volumes = volumetrictool::listVolumes();
                const services::VolumetricVolumeStatus* volume = volumetrictool::volumeByUuid(volumes, result.volumeId);
                bool startOutside = true;
                bool endOutside = true;
                for (const services::VolumetricVolumeStatus& candidate : volumes)
                {
                    if (candidate.baked)
                    {
                        startOutside = startOutside && !volumetrictool::boxContains(candidate, start.point);
                        endOutside = endOutside && !volumetrictool::boxContains(candidate, end.point);
                    }
                }

                std::vector<std::string> warnings;
                if (volume != nullptr && volume->stale)
                {
                    warnings.push_back(volumetrictool::volumeLabel(*volume) + " is stale: its box or settings changed "
                                       "since the grid was baked, so this path follows the old grid; volumetric_bake it "
                                       "again");
                }

                const std::optional<glm::vec3> first =
                    hasPath ? std::optional<glm::vec3>(result.waypoints.front()) : std::nullopt;
                const std::optional<glm::vec3> last =
                    hasPath ? std::optional<glm::vec3>(result.waypoints.back()) : std::nullopt;
                nlohmann::json path = nlohmann::json::array();
                if (hasPath)
                {
                    for (const glm::vec3& waypoint : result.waypoints)
                    {
                        path.push_back(env::vec3Json(waypoint));
                    }
                }
                const float length = hasPath ? pathLength(result.waypoints) : 0.0f;
                const std::string reason = pathReason(result.status, snapRadius, startOutside, endOutside,
                                                      last.has_value() ? glm::distance(*last, end.point) : 0.0f);

                nlohmann::json structured = nlohmann::json::object();
                structured["found"] = result.status == ::volumetric::VolumePathStatus::Found;
                structured["partial"] = result.status == ::volumetric::VolumePathStatus::Partial;
                structured["status"] = ::volumetric::volumePathStatusName(result.status);
                if (!reason.empty())
                {
                    structured["reason"] = reason;
                }
                structured["volume"] = volume != nullptr ? entityId(volume->entity) : nlohmann::json(nullptr);
                structured["path"] = std::move(path);
                structured["length"] = env::floatJson(length);
                structured["start"] = endpointJson(start, navigableAt(start.point), result.startSnapped, first);
                structured["end"] = endpointJson(end, navigableAt(end.point), result.endSnapped, last);
                structured["nodesExpanded"] = result.nodesExpanded;
                if (show)
                {
                    structured["shown"] = showPath(result, start, end, hasPath, showSeconds, warnings);
                }
                structured["warnings"] = warnings;

                std::string text;
                if (hasPath)
                {
                    text = std::string(result.status == ::volumetric::VolumePathStatus::Found ? "Found" : "Partial") +
                           " path: " + countText(result.waypoints.size(), "waypoint") + ", " + numberText(length) +
                           " m" + (volume != nullptr ? " through " + volumetrictool::volumeLabel(*volume) : std::string()) +
                           ".";
                }
                else
                {
                    text = std::string("No path (") + ::volumetric::volumePathStatusName(result.status) + ").";
                }
                if (!reason.empty())
                {
                    text += " " + reason + ".";
                }
                return reply(std::move(structured), std::move(text));
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // volumetric_clear
        // ------------------------------------------------------------------

        void registerVolumetricClear(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "volumetric_clear";
            tool.title = "Clear volumetric navigation";
            tool.description =
                "Unload the baked navigation grid of a VolumetricNavVolume (default: every volume) and clear its "
                "bakedRef, so flying agents cannot path there until the next volumetric_bake. The .vfVolNav files stay "
                "on disk. Not undoable: run scene_save afterwards, or the saved scene still references the files and a "
                "reload loads them again. Refused in Play mode and while a scene load is pending. Returns {cleared: "
                "[{volume, name, wasBaked, file}], note}.";
            tool.inputSchema = closedObject({
                {"volume", schema::entity("VolumetricNavVolume entity to clear. Default: every volume of the scene.")}
            });
            tool.destructive = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"volume"}, "volumetric_clear");
                const ArgReader reader(args);
                const std::optional<uint32_t> volumeId = reader.optEntity("volume");
                if (const std::optional<std::string> blocked = env::editBlocker("clear volumetric navigation"))
                {
                    return ToolResult::error(*blocked);
                }

                const std::vector<services::VolumetricVolumeStatus> volumes = volumetrictool::listVolumes();
                const services::VolumetricVolumeStatus* only =
                    volumeId.has_value() ? &volumetrictool::requireVolume(volumes, *volumeId, "volume") : nullptr;

                nlohmann::json cleared = nlohmann::json::array();
                for (const services::VolumetricVolumeStatus& volume : volumes)
                {
                    if (only != nullptr && volume.entity != only->entity)
                    {
                        continue;
                    }
                    nlohmann::json entry = nlohmann::json::object();
                    entry["volume"] = entityId(volume.entity);
                    entry["name"] = volume.name;
                    entry["wasBaked"] = volume.baked;
                    entry["file"] = volumetrictool::reportedFile(volume.file);
                    cleared.push_back(std::move(entry));
                }

                nlohmann::json structured = nlohmann::json::object();
                if (cleared.empty())
                {
                    structured["cleared"] = std::move(cleared);
                    structured["note"] = "The scene has no VolumetricNavVolume.";
                    return reply(std::move(structured), "The scene has no VolumetricNavVolume; nothing was cleared.");
                }

                events::volumetric::ClearVolumetricNavCommand command;
                command.volume = only != nullptr ? only->entity : services::EntityHandle::invalid();
                events::EventDispatcher::instance().execute(command);

                const std::string text = "Cleared " + countText(cleared.size(), "volume") +
                                         ". Run scene_save so the saved scene stops referencing their files.";
                structured["cleared"] = std::move(cleared);
                structured["note"] = clearNote;
                return reply(std::move(structured), text);
            };
            registry.add(std::move(tool));
        }
    }

    void registerVolumetricTools(ToolRegistry& registry, const ToolContext& context)
    {
        registerVolumetricBake(registry, context);
        registerVolumetricGetInfo(registry);
        registerVolumetricFindPath(registry);
        registerVolumetricClear(registry);
    }
}
