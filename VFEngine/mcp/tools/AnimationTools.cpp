#include "CoreTools.hpp"
#include "AnimationToolSupport.hpp"
#include "EnvironmentToolSupport.hpp"
#include "PathSandbox.hpp"
#include "SettingsTable.hpp"
#include "TerrainToolSupport.hpp"
#include "ToolHelpers.hpp"
#include "../protocol/ArgReader.hpp"
#include "../undo/AnimatorAssetUndo.hpp"
#include "../undo/McpUndo.hpp"
#include "../undo/McpUndoCommands.hpp"

#include "events/EventDispatcher.hpp"
#include "events/animation/AnimationAuthoringEvents.hpp"
#include "events/animation/AnimatorEvents.hpp"
#include "events/asset/AssetDatabaseEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// VK-1656 animation tools: animation_list_clips, animation_get_state_machine, animation_set_state_machine,
// animation_preview, animation_get_runtime_state. The Animator component itself is the "Animator" binding of
// the component_* tools (AnimatorBinding.cpp); IK and sockets have their own groups.
namespace mcp::tools
{
    namespace
    {
        namespace fs = std::filesystem;
        namespace authoring = events::animationAuthoring;
        namespace runtime = services::events::animator;

        constexpr std::chrono::milliseconds listClipsTimeout{30000};
        constexpr int64_t maxListedClips = 500;
        constexpr int64_t defaultListedClips = 100;
        constexpr int64_t maxLayer = 255;
        constexpr std::size_t maxPreviewEntities = 64;
        constexpr std::size_t maxAssignTo = 256;
        constexpr double maxEntityId = static_cast<double>(std::numeric_limits<uint32_t>::max());

        std::string lowercase(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        std::string describe(const services::EntityData& entity)
        {
            return std::to_string(toId(entity.handle)) + " ('" + entity.name + "')";
        }

        // A sandboxed, existing file with `extension` (argument `name`).
        fs::path requireProjectFile(const fs::path& root, const std::string& path, std::string_view extension,
                                    const char* name)
        {
            std::string error;
            const std::optional<fs::path> file = resolveInside(root, path, error, extension);
            if (!file.has_value())
            {
                throw ArgError(std::string("argument '") + name + "': " + error);
            }
            std::error_code ec;
            if (!fs::is_regular_file(*file, ec))
            {
                throw ArgError(std::string("argument '") + name + "': '" + path + "' does not exist");
            }
            return *file;
        }

        // An entity id taken from an array element.
        services::EntityData requireListedEntity(const nlohmann::json& value, const std::string& name)
        {
            const auto id = static_cast<uint32_t>(env::detail::checkedInteger(value, 0.0, maxEntityId, name));
            const std::optional<services::EntityData> entity = findEntity(toHandle(id));
            if (!entity.has_value())
            {
                throw std::runtime_error("Entity " + std::to_string(id) + " (argument '" + name +
                                         "') does not exist; use entity_find or scene_get_hierarchy");
            }
            return *entity;
        }

        std::size_t countIssues(const std::vector<animator::GraphIssue>& issues, animator::GraphIssue::Level level)
        {
            return static_cast<std::size_t>(std::count_if(issues.begin(), issues.end(),
                                                          [level](const animator::GraphIssue& issue) { return issue.level == level; }));
        }

        nlohmann::json vec2Schema(std::string description)
        {
            return {
                {"type", "array"},
                {"items", {{"type", "number"}}},
                {"minItems", 2},
                {"maxItems", 2},
                {"description", std::move(description)}
            };
        }

        nlohmann::json scalarSchema(std::string description)
        {
            return {{"type", nlohmann::json::array({"number", "boolean"})}, {"description", std::move(description)}};
        }

        // ------------------------------------------------------------------
        // The animator a tool addresses
        // ------------------------------------------------------------------

        struct AnimatorTarget
        {
            fs::path file;                              // absolute; empty when the entity has no animator
            bool exists = false;
            std::optional<services::EntityData> entity; // set when addressed through 'entity'
        };

        // 'animatorRef' (a sandboxed .vfAnimator path) or 'entity' (its assigned animator). `forWrite` also
        // requires an entity's animator to lie inside the project.
        AnimatorTarget resolveAnimatorTarget(const ArgReader& reader, const fs::path& root, bool forWrite)
        {
            if (reader.has("animatorRef") == reader.has("entity"))
            {
                throw ArgError("pass exactly one of 'animatorRef' (a .vfAnimator path) or 'entity'");
            }
            AnimatorTarget target;
            std::error_code ec;
            if (reader.has("animatorRef"))
            {
                std::string error;
                const std::optional<fs::path> file =
                    resolveInside(root, reader.requireString("animatorRef"), error, ".vfAnimator");
                if (!file.has_value())
                {
                    throw ArgError("argument 'animatorRef': " + error);
                }
                target.file = *file;
                target.exists = fs::is_regular_file(*file, ec);
                return target;
            }

            target.entity = requireEntity(reader, "entity");
            const std::optional<services::MeshData> mesh = anim::meshData(target.entity->handle);
            if (!mesh.has_value() || !mesh->animatorRef.isValid())
            {
                return target;
            }
            const std::string path = mesh->animatorRef.resolve();
            if (path.empty())
            {
                throw std::runtime_error("The animator of entity " + describe(*target.entity) + " (guid:" +
                                         mesh->animatorRef.toHexString() + ") cannot be found");
            }
            target.file = fs::path(path);
            if (forWrite)
            {
                std::string error;
                const std::optional<fs::path> inside = resolveInside(root, pathToUtf8(target.file), error);
                if (!inside.has_value())
                {
                    throw std::runtime_error("The animator of entity " + describe(*target.entity) + " ('" +
                                             genericPathToUtf8(target.file) +
                                             "') lies outside the project and cannot be edited here");
                }
                target.file = *inside;
            }
            target.exists = fs::is_regular_file(target.file, ec);
            if (!target.exists)
            {
                throw std::runtime_error("The animator of entity " + describe(*target.entity) + " ('" +
                                         projectRelativeUtf8(root, target.file) + "') is missing on disk");
            }
            return target;
        }

        std::string noAnimatorText(const services::EntityData& entity)
        {
            return "Entity " + describe(entity) + " has no Animator (component_add {type:\"Animator\", data:{defaultClip}} "
                   "adds one).";
        }

        // The animation_get_state_machine object for `layer` of `data`.
        nlohmann::json describeAnimator(const fs::path& root, const fs::path& file, const animator::AnimatorData& data,
                                        uint32_t layer, const std::vector<animator::GraphIssue>& issues)
        {
            const anim::ClipNamer namer = anim::projectNamer(root);
            nlohmann::json out = anim::graphJson(anim::stateMachineLayer(data, layer), namer);
            out["animator"] = projectRelativeUtf8(root, file);
            out["name"] = data.name;
            out["layered"] = !data.layers.empty();
            out["layers"] = anim::layersJson(data, namer);
            out["layer"] = layer;
            out["issues"] = anim::issuesJson(issues);
            out["usedBy"] = anim::usersJson(anim::animatorUsers(asset::AssetRef::fromPath(file.string())));
            return out;
        }

        std::string animatorSummary(const nlohmann::json& out, const std::vector<animator::GraphIssue>& issues)
        {
            std::string text = out["animator"].get<std::string>() + " (layer " + std::to_string(out["layer"].get<uint32_t>()) +
                               "): " + std::to_string(out["states"].size()) + " state(s), " +
                               std::to_string(out["transitions"].size()) + " transition(s), " +
                               std::to_string(out["parameters"].size()) + " parameter(s)";
            if (out["defaultState"].is_string())
            {
                text += ", starts in " + out["defaultState"].get<std::string>();
            }
            text += "; " + std::to_string(countIssues(issues, animator::GraphIssue::Level::Error)) + " error(s), " +
                    std::to_string(countIssues(issues, animator::GraphIssue::Level::Warning)) + " warning(s); used by " +
                    std::to_string(out["usedBy"].size()) + " entit" + (out["usedBy"].size() == 1 ? "y." : "ies.");
            return text;
        }

        // ------------------------------------------------------------------
        // animation_list_clips
        // ------------------------------------------------------------------

        void registerListClips(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "animation_list_clips";
            tool.title = "List animation clips";
            tool.description =
                "List the project's animation clips (.vfAnim, from the asset database) that fit a skinned mesh: "
                "{entity} uses the entity's Mesh and its retargetRef, {mesh, retarget?} names them directly. Played "
                "natively, a clip drives a bone through the channel of the same name; through a retarget map, a bone "
                "is driven when its humanoid role maps to a source bone the clip animates. coverage = driven bones / "
                "skeleton bones; compatible = coverage >= minCoverage and rootBone is driven (the skeleton's first "
                "root bone, or through a retarget map the bone bound to the Hips role). Model "
                "imports (FBX, GLB, DAE, ...) write one <model>_<take>.vfAnim per animation take next to the meshes. "
                "Returns {mesh, retarget, via, skeletonBones, rootBone, minCoverage, clips:[{path, name, "
                "durationSeconds, ticksPerSecond, channels, matchedBones, skeletonBones, coverage, rootMatched, "
                "compatible, via}], total, returned, truncated, incompatible, unreadable}, best coverage first. Use a "
                "clip in component_add {type:\"Animator\", data:{defaultClip}}, animation_set_state_machine or "
                "animation_preview.";
            tool.inputSchema = closedObject({
                {"entity", schema::entity("Entity whose Mesh (and retargetRef) the clips must fit")},
                {"mesh", schema::string("Instead of 'entity': a skinned .vfMesh path (project-relative)")},
                {"retarget", schema::string("With 'mesh': a .vfretarget map the clips play through")},
                {"directory", schema::string("Only clips under this project folder")},
                {"contains", schema::string("Case-insensitive substring filter on the clip path")},
                {"minCoverage", schema::numberRange("Fraction of the skeleton's bones a compatible clip drives. "
                                                    "Default 0.5.", 0.0, 1.0)},
                {"includeIncompatible", schema::boolean("Also list incompatible and unreadable clips. Default false.")},
                {"max", schema::integerRange("Maximum clips returned. Default 100.", 1, maxListedClips)}
            });
            tool.readOnly = true;
            tool.timeout = listClipsTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"entity", "mesh", "retarget", "directory", "contains", "minCoverage",
                                              "includeIncompatible", "max"}, "animation_list_clips");
                const ArgReader reader(args);
                if (reader.has("entity") == reader.has("mesh"))
                {
                    throw ArgError("pass exactly one of 'entity' or 'mesh'");
                }
                if (reader.has("entity") && reader.has("retarget"))
                {
                    throw ArgError("'retarget' goes with 'mesh'; an entity plays clips through its own retargetRef");
                }
                const auto minCoverage = static_cast<float>(reader.optNumberInRange("minCoverage", 0.0, 1.0, 0.5));
                const bool includeIncompatible = reader.optBool("includeIncompatible", false);
                const auto max = static_cast<std::size_t>(reader.optIntInRange("max", 1, maxListedClips, defaultListedClips));
                const std::string needle = lowercase(reader.optString("contains"));
                const fs::path root = terrainProjectRoot();

                std::optional<fs::path> directory;
                if (reader.has("directory"))
                {
                    std::string error;
                    directory = resolveInside(root, reader.requireString("directory"), error);
                    if (!directory.has_value())
                    {
                        throw ArgError("argument 'directory': " + error);
                    }
                }

                // Engine paths of the mesh and of the retarget map ("" = played natively).
                std::string meshPath;
                std::string retargetPath;
                if (reader.has("entity"))
                {
                    const services::EntityData entity = requireEntity(reader, "entity");
                    const std::optional<services::MeshData> mesh = anim::meshData(entity.handle);
                    if (!mesh.has_value() || !mesh->meshRef.isValid())
                    {
                        return ToolResult::error("Entity " + describe(entity) + " has no Mesh with a meshRef");
                    }
                    meshPath = mesh->meshRef.resolve();
                    if (meshPath.empty())
                    {
                        return ToolResult::error("The mesh asset of entity " + describe(entity) + " cannot be found");
                    }
                    if (mesh->retargetRef.isValid())
                    {
                        retargetPath = mesh->retargetRef.resolve();
                        if (retargetPath.empty())
                        {
                            return ToolResult::error("The retarget map of entity " + describe(entity) + " cannot be found");
                        }
                    }
                }
                else
                {
                    meshPath = requireProjectFile(root, reader.requireString("mesh"), ".vfMesh", "mesh").string();
                    if (reader.has("retarget"))
                    {
                        retargetPath =
                            requireProjectFile(root, reader.requireString("retarget"), ".vfretarget", "retarget").string();
                    }
                }

                auto& dispatcher = events::EventDispatcher::instance();
                const std::string meshText = projectRelativeUtf8(root, fs::path(meshPath));
                const services::MeshSkeletonInfo skeleton = anim::meshSkeleton(meshPath);
                if (!skeleton.success)
                {
                    return ToolResult::error("Cannot read the skeleton of '" + meshText + "': " + skeleton.error);
                }
                if (!skeleton.skinned)
                {
                    return ToolResult::error("'" + meshText + "' is a static mesh (no skeleton): no clip can animate it");
                }

                // The mapping keeps references into `binding`, which outlives it in this scope.
                std::optional<services::RetargetBindingInfo> binding;
                std::optional<retargeting::RetargetBoneMapping> mapping;
                if (!retargetPath.empty())
                {
                    authoring::GetRetargetBindingQuery query;
                    query.retargetPath = retargetPath;
                    binding = dispatcher.query(query);
                    if (!binding->success)
                    {
                        return ToolResult::error("Cannot read the retarget map '" +
                                                 projectRelativeUtf8(root, fs::path(retargetPath)) + "': " + binding->error);
                    }
                    mapping.emplace(binding->sourceRig, binding->targetRig, binding->map);
                }
                const char* via = mapping.has_value() ? "retarget" : "native";

                events::assetdb::GetAssetsByTypeQuery assets;
                assets.type = resource::AssetType::Animation;
                std::vector<std::string> paths;
                std::vector<std::string> relatives;
                for (const events::assetdb::AssetEntryData& entry : dispatcher.query(assets))
                {
                    const fs::path file(entry.path);
                    if (directory.has_value())
                    {
                        std::error_code ec;
                        const fs::path canonical = fs::weakly_canonical(file, ec);
                        if (ec || !detail::isWithin(*directory, canonical))
                        {
                            continue;
                        }
                    }
                    std::string relative = projectRelativeUtf8(root, file);
                    if (!needle.empty() && lowercase(relative).find(needle) == std::string::npos)
                    {
                        continue;
                    }
                    paths.push_back(entry.path);
                    relatives.push_back(std::move(relative));
                }

                std::vector<services::AnimationClipSummary> summaries;
                if (!paths.empty())
                {
                    authoring::GetAnimationClipSummariesQuery query;
                    query.paths = paths;
                    summaries = dispatcher.query(query);
                    if (summaries.size() != paths.size())
                    {
                        return ToolResult::error("The clip summary query answered " + std::to_string(summaries.size()) +
                                                 " of " + std::to_string(paths.size()) + " clips");
                    }
                }

                struct Listed
                {
                    float coverage = 0.0f;
                    std::string path;
                    nlohmann::json item;
                };
                std::vector<Listed> listed;
                std::size_t incompatible = 0;
                std::size_t unreadable = 0;
                for (std::size_t i = 0; i < summaries.size(); ++i)
                {
                    const services::AnimationClipSummary& summary = summaries[i];
                    if (!summary.success)
                    {
                        ++unreadable;
                        if (includeIncompatible)
                        {
                            listed.push_back({-1.0f, relatives[i], nlohmann::json{
                                {"path", relatives[i]}, {"compatible", false}, {"error", summary.error}}});
                        }
                        continue;
                    }
                    const anim::ClipCoverage coverage = mapping.has_value()
                        ? anim::retargetCoverage(skeleton, *mapping, summary.channelNames, minCoverage)
                        : anim::nativeCoverage(skeleton, summary.channelNames, minCoverage);
                    if (!coverage.compatible)
                    {
                        ++incompatible;
                        if (!includeIncompatible)
                        {
                            continue;
                        }
                    }
                    listed.push_back({coverage.coverage, relatives[i], nlohmann::json{
                        {"path", relatives[i]},
                        {"name", summary.name},
                        {"durationSeconds", env::floatJson(summary.durationSeconds)},
                        {"ticksPerSecond", env::floatJson(summary.ticksPerSecond)},
                        {"channels", summary.channelNames.size()},
                        {"matchedBones", coverage.matchedBones},
                        {"skeletonBones", coverage.skeletonBones},
                        {"coverage", std::round(static_cast<double>(coverage.coverage) * 1000.0) / 1000.0},
                        {"rootMatched", coverage.rootMatched},
                        {"compatible", coverage.compatible},
                        {"via", via}
                    }});
                }
                std::sort(listed.begin(), listed.end(), [](const Listed& a, const Listed& b)
                {
                    return a.coverage != b.coverage ? a.coverage > b.coverage : a.path < b.path;
                });

                const std::size_t total = listed.size();
                const std::size_t returned = std::min(total, max);
                nlohmann::json clips = nlohmann::json::array();
                for (std::size_t i = 0; i < returned; ++i)
                {
                    clips.push_back(std::move(listed[i].item));
                }
                const std::optional<std::size_t> rootBone =
                    mapping.has_value() ? anim::retargetRootBone(skeleton, *mapping) : anim::firstRootBone(skeleton);
                nlohmann::json out{
                    {"mesh", meshText},
                    {"retarget", retargetPath.empty() ? nlohmann::json(nullptr)
                                                      : nlohmann::json(projectRelativeUtf8(root, fs::path(retargetPath)))},
                    {"via", via},
                    {"skeletonBones", skeleton.boneNames.size()},
                    {"rootBone", rootBone.has_value() ? nlohmann::json(skeleton.boneNames[*rootBone]) : nlohmann::json(nullptr)},
                    {"minCoverage", env::floatJson(minCoverage)},
                    {"clips", std::move(clips)},
                    {"total", total},
                    {"returned", returned},
                    {"truncated", total > returned},
                    {"incompatible", incompatible},
                    {"unreadable", unreadable}
                };
                std::string summary = std::to_string(total) + " clip(s) listed for " + meshText + " (" + via + "), " +
                                      std::to_string(incompatible) + " incompatible, " + std::to_string(unreadable) +
                                      " unreadable.";
                if (total > returned)
                {
                    summary += " Showing the first " + std::to_string(returned) + ".";
                }
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // animation_get_state_machine / animation_set_state_machine
        // ------------------------------------------------------------------

        void registerGetStateMachine(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "animation_get_state_machine";
            tool.title = "Get animator state machine";
            tool.description =
                "Read an animator (.vfAnimator) in the form animation_set_state_machine takes: {animator, name, "
                "layered, layers:[{index, name, sourceMode, weight, blendMode, boneMask, states}], layer, defaultState, "
                "parameters:[{name, type, default}], states:[{id, name, clip, playbackSpeed, loop, position, events "
                "(count), blendTree?}], transitions:[{id, from (a state or \"Any\"), to, conditions:[{parameter, op, "
                "value}], blendDuration, hasExitTime, exitTime, priority}], issues:[{level, layer, message}] (validation "
                "of the whole file), usedBy:[{entity, name}]}. Address it by animatorRef (project-relative path) or by "
                "entity (its assigned animator); 'layer' picks a state-machine layer of a layered file (default 0, the "
                "base layer).";
            tool.inputSchema = closedObject({
                {"animatorRef", schema::string(".vfAnimator path (project-relative)")},
                {"entity", schema::entity("Instead of animatorRef: the entity whose assigned animator to read")},
                {"layer", schema::integerRange("State-machine layer. Default 0 (the base layer).", 0, maxLayer)}
            });
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"animatorRef", "entity", "layer"}, "animation_get_state_machine");
                const ArgReader reader(args);
                const auto layer = static_cast<uint32_t>(reader.optIntInRange("layer", 0, maxLayer, 0));
                const fs::path root = terrainProjectRoot();
                const AnimatorTarget target = resolveAnimatorTarget(reader, root, false);
                if (target.file.empty())
                {
                    return ToolResult::error(noAnimatorText(*target.entity));
                }
                const std::string animatorText = projectRelativeUtf8(root, target.file);
                if (!target.exists)
                {
                    return ToolResult::error("'" + animatorText + "' does not exist (animation_set_state_machine "
                                             "{animatorRef, create:true, ...} creates it)");
                }
                const services::AnimatorAssetLoadResult loaded = anim::loadAnimator(target.file.string());
                if (!loaded.success)
                {
                    return ToolResult::error("Cannot read '" + animatorText + "': " + loaded.error);
                }

                const std::vector<animator::GraphIssue> issues = animator::validateAnimator(loaded.data);
                nlohmann::json out = describeAnimator(root, target.file, loaded.data, layer, issues);
                const std::string summary = animatorSummary(out, issues);
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        nlohmann::json setStateMachineSchema()
        {
            const nlohmann::json parameter = closedObject({
                {"name", schema::string("Parameter name (scripts set it with Animator::setFloat / setBool / setTrigger)")},
                {"type", schema::enumString("Required for a new parameter", {"Float", "Int", "Bool", "Trigger"})},
                {"default", scalarSchema("Initial value: a number (Float / Int) or true/false (Bool / Trigger)")}
            }, {"name"});

            const nlohmann::json entry = closedObject({
                {"clip", schema::string(".vfAnim path (project-relative)")},
                {"threshold", schema::number("1D: the parameter value at which this clip plays alone")},
                {"position", vec2Schema("2D: [x, y] in the two parameters' space")}
            }, {"clip"});
            nlohmann::json blendTree = closedObject({
                {"type", schema::enumString("Blend tree kind", {"1D", "2D"})},
                {"parameter", schema::string("Float parameter of the (X) axis")},
                {"parameterY", schema::string("2D: Float parameter of the Y axis")},
                {"entries", schema::array(entry, "The blended clips")}
            }, {"type", "parameter", "entries"});
            blendTree["type"] = nlohmann::json::array({"object", "null"});
            blendTree["description"] = "A blend tree the state plays instead of its clip; null removes it";

            const nlohmann::json state = closedObject({
                {"name", schema::string("State name, matched against the existing states (not \"Any\")")},
                {"rename", schema::string("New name; the state keeps its id and transitions")},
                {"clip", schema::string(".vfAnim path (project-relative); \"\" clears")},
                {"playbackSpeed", schema::number("Speed multiplier, > 0 (default 1)")},
                {"loop", schema::boolean("Loop the clip (default true)")},
                {"position", vec2Schema("Node position [x, y] in the Animator Editor, rounded to whole pixels "
                                        "(default: automatic)")},
                {"blendTree", std::move(blendTree)}
            }, {"name"});

            const nlohmann::json condition = closedObject({
                {"parameter", schema::string("Parameter name")},
                {"op", schema::enumString("Comparison (may be omitted for a Trigger)", {">", "<", ">=", "<=", "==", "!="})},
                {"value", scalarSchema("Typed by the parameter: a number (Float / Int), true/false (Bool), true "
                                       "(Trigger, may be omitted)")}
            }, {"parameter"});
            const nlohmann::json transition = closedObject({
                {"from", schema::string("Source state name, or \"Any\" (the Any State)")},
                {"to", schema::string("Target state name")},
                {"conditions", schema::array(condition, "All must hold. Without conditions the transition needs "
                                                        "hasExitTime")},
                {"blendDuration", schema::numberRange("Cross-fade in seconds (default 0.25)", 0.0, 60.0)},
                {"hasExitTime", schema::boolean("Wait for exitTime (default: true when exitTime is given)")},
                {"exitTime", schema::numberRange("Normalized time 0-1 of the source state at which it may fire",
                                                 0.0, 1.0)},
                {"priority", schema::integer("Lower is tested first (default 0)")}
            }, {"from", "to"});

            const nlohmann::json pair = closedObject({
                {"from", schema::string("Source state name or \"Any\"")},
                {"to", schema::string("Target state name")}
            }, {"from", "to"});
            nlohmann::json remove = closedObject({
                {"parameters", schema::array(schema::string("Parameter name"), "Parameters to delete")},
                {"states", schema::array(schema::string("State name"), "States to delete, with their transitions")},
                {"transitions", schema::array(pair, "Every transition of each pair is deleted")}
            });
            remove["description"] = "Deletions, applied first, by the current names";

            return closedObject({
                {"animatorRef", schema::string(".vfAnimator path (project-relative)")},
                {"entity", schema::entity("Instead of animatorRef: the entity whose assigned animator to edit")},
                {"create", schema::boolean("Create the animator when it does not exist. Default false.")},
                {"layer", schema::integerRange("State-machine layer to edit. Default 0.", 0, maxLayer)},
                {"replace", schema::boolean("Each given list becomes the layer's whole list. Default false.")},
                {"parameters", schema::array(parameter, "Parameters to add or update, by name")},
                {"states", schema::array(state, "States to add or update, by name")},
                {"transitions", schema::array(transition, "Each listed (from, to) pair replaces all of that pair's "
                                                          "transitions, in this order")},
                {"defaultState", schema::string("The state the animator starts in")},
                {"remove", std::move(remove)},
                {"assignTo", schema::array(schema::entity(), "Entities with a skinned Mesh to assign the animator to")}
            });
        }

        std::vector<services::EntityHandle> assignTargets(const ArgReader& reader)
        {
            std::vector<services::EntityHandle> targets;
            if (!reader.has("assignTo"))
            {
                return targets;
            }
            const nlohmann::json& list = reader.raw("assignTo");
            if (!list.is_array() || list.size() > maxAssignTo)
            {
                throw ArgError("argument 'assignTo' must be an array of at most " + std::to_string(maxAssignTo) +
                               " entity ids");
            }
            for (std::size_t i = 0; i < list.size(); ++i)
            {
                const services::EntityHandle handle =
                    requireListedEntity(list[i], "assignTo[" + std::to_string(i) + "]").handle;
                if (std::find(targets.begin(), targets.end(), handle) == targets.end())
                {
                    targets.push_back(handle);
                }
            }
            return targets;
        }

        void registerSetStateMachine(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "animation_set_state_machine";
            tool.title = "Edit animator state machine";
            tool.description =
                "Create or edit an animator's state machine (a .vfAnimator), saved at once; one undo step. Target: "
                "animatorRef (project-relative path) or entity (its assigned animator). create:true creates a missing "
                "animatorRef file, or - for an entity without an Animator - <mesh folder>/<entity name>.vfAnimator, "
                "assigned to it (creating a file is not undoable). 'layer' picks a state-machine layer of a layered "
                "file (default 0; a single-graph file has only 0). Applied in this order: remove {parameters, states "
                "(their transitions go too; removing the default state needs defaultState), transitions [{from, to}] "
                "(every transition of the pair)} by the current names; parameters [{name, type: Float | Int | Bool | "
                "Trigger, default}] upserted by name; states [{name, rename?, clip?, playbackSpeed?, loop?, position?, "
                "blendTree?}] upserted by name (rename keeps the id and transitions; new states are laid out "
                "automatically; clip \"\" clears; blendTree {type:\"1D\", parameter (a Float), entries:[{clip, "
                "threshold}]} or {type:\"2D\", parameter, parameterY, entries:[{clip, position:[x,y]}]}, null removes "
                "it; animation events are kept); defaultState; transitions [{from: state | \"Any\", to, conditions:"
                "[{parameter, op: > < >= <= == !=, value}], blendDuration (s, default 0.25), hasExitTime, exitTime "
                "(0-1, giving it enables exit time), priority (lower first)}] - each listed (from, to) pair replaces "
                "ALL of that pair's transitions, in the given order. A condition's value is typed by its parameter: "
                "Float a number, Int an integer, Bool true/false with == or !=, Trigger only == true (op and value may "
                "be omitted). replace:true makes each given list the layer's whole list (states matched by name keep "
                "their ids). Clips are project-relative .vfAnim paths (animation_list_clips). assignTo:[entity ids] "
                "assigns the animator to entities with a skinned Mesh. The result is validated: errors fail the call "
                "and nothing is saved; warnings come back in issues. Refused in Play mode. Undo restores the previous "
                "file content (skipped when the file changed since) and the previous assignments; live animators "
                "reload after every save. Returns animation_get_state_machine's object plus {created, saved, changes, "
                "assigned}.";
            tool.inputSchema = setStateMachineSchema();
            tool.destructive = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"animatorRef", "entity", "create", "layer", "replace", "parameters", "states",
                                              "transitions", "defaultState", "remove", "assignTo"},
                                       "animation_set_state_machine");
                if (const std::optional<std::string> blocked = env::editBlocker("edit an animator"))
                {
                    return ToolResult::error(*blocked);
                }
                const ArgReader reader(args);
                const bool create = reader.optBool("create", false);
                const auto layer = static_cast<uint32_t>(reader.optIntInRange("layer", 0, maxLayer, 0));

                nlohmann::json edit = nlohmann::json::object();
                for (const char* key : {"replace", "parameters", "states", "transitions", "defaultState", "remove"})
                {
                    if (reader.has(key))
                    {
                        edit[key] = reader.raw(key);
                    }
                }
                const bool hasEdit = edit.contains("parameters") || edit.contains("states") ||
                                     edit.contains("transitions") || edit.contains("defaultState") || edit.contains("remove");

                const std::vector<services::EntityHandle> assignTo = assignTargets(reader);
                const fs::path root = terrainProjectRoot();
                const AnimatorTarget target = resolveAnimatorTarget(reader, root, true);

                fs::path file = target.file;
                bool creating = false;
                std::optional<services::EntityHandle> createdFor;
                if (file.empty())
                {
                    if (!create)
                    {
                        return ToolResult::error(noAnimatorText(*target.entity) +
                                                 " Or pass create:true to create one next to its mesh.");
                    }
                    if (const std::optional<std::string> problem = anim::animatorHostProblem(target.entity->handle))
                    {
                        return ToolResult::error("Cannot create an animator for it: " + *problem);
                    }
                    file = anim::defaultAnimatorPath(target.entity->handle, root,
                                                     "pass animatorRef with a project-relative .vfAnimator path and "
                                                     "create:true, then assignTo");
                    creating = true;
                    createdFor = target.entity->handle;
                }
                else if (!target.exists)
                {
                    if (!create)
                    {
                        return ToolResult::error("'" + projectRelativeUtf8(root, file) +
                                                 "' does not exist; pass create:true to create it");
                    }
                    creating = true;
                }
                if (!hasEdit && !creating && assignTo.empty())
                {
                    throw ArgError("nothing to do: pass parameters, states, transitions, defaultState, remove or assignTo");
                }
                // Every assignee must be able to play it before anything is written.
                for (const services::EntityHandle& entity : assignTo)
                {
                    if (const std::optional<std::string> problem = anim::animatorHostProblem(entity))
                    {
                        return ToolResult::error("Cannot assign the animator: " + *problem);
                    }
                }

                const std::string animatorText = projectRelativeUtf8(root, file);
                services::AnimatorAssetLoadResult loaded;
                if (!creating)
                {
                    loaded = anim::loadAnimator(file.string());
                    if (!loaded.success)
                    {
                        return ToolResult::error("Cannot read '" + animatorText + "': " + loaded.error);
                    }
                }
                animator::AnimatorData data = creating ? anim::emptyAnimator(pathToUtf8(file.stem())) : loaded.data;

                anim::EditSummary changes;
                if (hasEdit)
                {
                    changes = anim::applyAnimatorEdit(data, edit, layer,
                        [root](const std::string& path, const std::string& argument)
                        {
                            return anim::resolveClip(root, path, argument);
                        });
                }
                else
                {
                    anim::stateMachineLayer(data, layer); // the 'layer' argument is still checked
                }

                const bool saving = hasEdit || creating;
                const std::vector<animator::GraphIssue> issues = animator::validateAnimator(data);
                if (saving && animator::hasErrors(issues))
                {
                    return ToolResult::error("'" + animatorText + "' was not saved; fix these errors and retry:\n" +
                                             anim::errorLines(issues));
                }

                auto& dispatcher = events::EventDispatcher::instance();
                services::AnimatorAssetSaveResult saved;
                if (creating)
                {
                    saved = anim::createAnimatorFile(file, data);
                }
                else if (hasEdit)
                {
                    authoring::SaveAnimatorAssetCommand command;
                    command.path = file.string();
                    command.data = data;
                    command.expectedContentHash = loaded.contentHash;
                    saved = dispatcher.execute(command);
                    if (!saved.success)
                    {
                        return ToolResult::error("Saving '" + animatorText + "' failed: " + saved.error);
                    }
                }

                // The asset entry first: a batch undoes newest first, so the assignments revert before the file.
                undo::UndoRecorder recorder("MCP: Edit animator " + pathToUtf8(file.stem()));
                if (!creating && hasEdit)
                {
                    recorder.add(std::make_unique<undo::AnimatorAssetUndo>(file.string(), loaded.data, data,
                                                                           saved.contentHash));
                }

                std::vector<services::EntityHandle> assignees;
                if (createdFor.has_value())
                {
                    assignees.push_back(*createdFor);
                }
                for (const services::EntityHandle& entity : assignTo)
                {
                    if (std::find(assignees.begin(), assignees.end(), entity) == assignees.end())
                    {
                        assignees.push_back(entity);
                    }
                }

                nlohmann::json assigned = nlohmann::json::array();
                try
                {
                    const asset::AssetRef animatorRef = asset::AssetRef::fromPath(file.string());
                    const nlohmann::json patch{{"animatorRef", animatorText}};
                    for (const services::EntityHandle& entity : assignees)
                    {
                        const std::optional<services::MeshData> mesh = anim::meshData(entity);
                        if (mesh.has_value() && mesh->animatorRef.isValid() && mesh->animatorRef == animatorRef)
                        {
                            continue; // already plays it
                        }
                        const std::optional<nlohmann::json> before = getBuiltinComponent("Animator", entity);
                        // An added Animator is undone by restoring the Mesh fields it lives in, so a retargetRef
                        // the Mesh already had survives the undo.
                        const std::optional<BuiltinHostFields> hostBefore =
                            before.has_value() ? std::nullopt : builtinHostFields("Animator", entity);
                        if (!before.has_value() && !addBuiltinComponent("Animator", entity))
                        {
                            throw std::runtime_error("entity " + std::to_string(toId(entity)) + " cannot carry an Animator");
                        }
                        const std::optional<nlohmann::json> after = patchBuiltinComponent("Animator", entity, patch);
                        if (!after.has_value())
                        {
                            throw std::runtime_error("entity " + std::to_string(toId(entity)) + " has no Mesh component");
                        }
                        const std::optional<BuiltinHostFields> hostAfter =
                            hostBefore.has_value() ? builtinHostFields("Animator", entity) : std::nullopt;
                        if (before.has_value())
                        {
                            recorder.add(std::make_unique<undo::BuiltinComponentPatchUndo>(toId(entity), "Animator",
                                                                                           *before, *after));
                        }
                        else if (hostBefore.has_value() && hostAfter.has_value())
                        {
                            recorder.add(std::make_unique<undo::BuiltinComponentPatchUndo>(
                                toId(entity), hostBefore->type, hostBefore->fields, hostAfter->fields));
                        }
                        else
                        {
                            recorder.add(std::make_unique<undo::BuiltinComponentPresenceUndo>(toId(entity), "Animator",
                                                                                              *after, true));
                        }
                        assigned.push_back(toId(entity));
                    }
                }
                catch (const std::exception& e)
                {
                    recorder.rollback();
                    std::string message = std::string("Assigning the animator failed (") + e.what() +
                                          "); the call was rolled back";
                    if (creating)
                    {
                        message += ", but the new file '" + animatorText + "' remains";
                    }
                    return ToolResult::error(message);
                }
                recorder.push();

                nlohmann::json out = describeAnimator(root, file, data, layer, issues);
                out["created"] = creating;
                out["saved"] = saving;
                out["changes"] = changes.toJson();
                out["assigned"] = std::move(assigned);
                std::string summary = std::string(creating ? "Created " : saving ? "Saved " : "Assigned ") +
                                      animatorSummary(out, issues);
                if (!out["assigned"].empty())
                {
                    summary += " Assigned to " + std::to_string(out["assigned"].size()) + " entit" +
                               (out["assigned"].size() == 1 ? "y." : "ies.");
                }
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // animation_preview
        // ------------------------------------------------------------------

        std::vector<services::EntityData> previewEntities(const ArgReader& reader)
        {
            if (reader.has("entity") == reader.has("entities"))
            {
                throw ArgError("pass exactly one of 'entity' or 'entities'");
            }
            std::vector<services::EntityData> entities;
            if (reader.has("entity"))
            {
                entities.push_back(requireEntity(reader, "entity"));
                return entities;
            }
            const nlohmann::json& list = reader.raw("entities");
            if (!list.is_array() || list.empty() || list.size() > maxPreviewEntities)
            {
                throw ArgError("argument 'entities' must hold 1-" + std::to_string(maxPreviewEntities) + " entity ids");
            }
            for (std::size_t i = 0; i < list.size(); ++i)
            {
                services::EntityData entity = requireListedEntity(list[i], "entities[" + std::to_string(i) + "]");
                const bool listed = std::any_of(entities.begin(), entities.end(), [&entity](const services::EntityData& other)
                {
                    return other.handle == entity.handle;
                });
                if (!listed)
                {
                    entities.push_back(std::move(entity));
                }
            }
            return entities;
        }

        // A state preview's parameter values, typed by the entity's own animator (which must have the state).
        std::vector<std::pair<std::string, animator::AnimatorParameterValue>> previewParameters(
            const services::EntityData& entity, const services::MeshData& mesh, const std::string& state,
            const nlohmann::json* parameters)
        {
            const std::string path = mesh.animatorRef.resolve();
            if (path.empty())
            {
                throw std::runtime_error("The animator of entity " + describe(entity) + " cannot be found");
            }
            const services::AnimatorAssetLoadResult loaded = anim::loadAnimator(path);
            if (!loaded.success)
            {
                throw std::runtime_error("Cannot read the animator of entity " + describe(entity) + ": " + loaded.error);
            }
            const animator::AnimatorGraph& base = anim::baseGraph(loaded.data);
            if (base.findStateByName(state) == nullptr)
            {
                std::string names;
                for (const animator::AnimatorState& candidate : base.states)
                {
                    names += names.empty() ? candidate.name : ", " + candidate.name;
                }
                throw ArgError("argument 'state': the animator of entity " + describe(entity) + " has no state \"" + state +
                               "\" in its base layer (states: " + (names.empty() ? std::string("none") : names) + ")");
            }

            std::vector<std::pair<std::string, animator::AnimatorParameterValue>> values;
            if (parameters == nullptr)
            {
                return values;
            }
            for (auto it = parameters->begin(); it != parameters->end(); ++it)
            {
                const animator::AnimatorParameter* declared = anim::findParameter(loaded.data, it.key());
                if (declared == nullptr)
                {
                    throw ArgError("argument 'parameters': the animator of entity " + describe(entity) +
                                   " has no parameter \"" + it.key() + "\"");
                }
                values.emplace_back(it.key(), anim::parameterValueFrom(declared->type, it.value(), "parameters." + it.key()));
            }
            return values;
        }

        nlohmann::json previewStatusJson(const std::optional<fs::path>& root,
                                         const std::optional<services::AnimationEditPreviewStatus>& status)
        {
            if (!status.has_value())
            {
                return nullptr;
            }
            const bool state = status->source == services::AnimationPreviewSource::State;
            return {
                {"source", state ? "state" : "clip"},
                {"state", state ? nlohmann::json(status->state) : nlohmann::json(nullptr)},
                {"clip", state ? nlohmann::json(nullptr) : nlohmann::json(projectRelativeUtf8(root, fs::path(status->clipPath)))},
                {"normalizedTime", env::floatJson(status->normalizedTime)},
                {"durationSeconds", env::floatJson(status->durationSeconds)},
                {"playing", status->playing}
            };
        }

        void registerPreview(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "animation_preview";
            tool.title = "Preview animation";
            tool.description =
                "Preview an animation in edit mode on entities that have an Animator, without changing the animator or "
                "the scene: 'state' plays a base-layer state of the entity's own animator ('parameters' {name: value} "
                "set its blend-tree inputs), 'clip' plays any .vfAnim (animation_list_clips shows which fit). With "
                "playing (default true) it plays from normalizedTime (0-1, default 0) at 'speed' (default 1); "
                "playing:false holds the pose at normalizedTime. 'loop' (default true) applies to clips. {entity | "
                "entities, stop:true} ends those previews, {stopAll:true} ends all, and {entity | entities} alone "
                "reports the running previews. A new preview replaces the entity's current one; play_start, a save of "
                "the entity's animator and removing it end previews too. Call viewport_screenshot to see the pose. Not "
                "undoable (nothing is saved); refused in Play mode.";
            tool.inputSchema = closedObject({
                {"entity", schema::entity("Entity with an Animator")},
                {"entities", schema::array(schema::entity(), "Instead of 'entity': up to 64 entities")},
                {"state", schema::string("A state of the entity's animator (base layer)")},
                {"clip", schema::string("Instead of 'state': a .vfAnim path (project-relative)")},
                {"normalizedTime", schema::numberRange("Start time (playing) or held pose time (paused), 0-1. "
                                                       "Default 0.", 0.0, 1.0)},
                {"playing", schema::boolean("Play (default true) or hold the pose at normalizedTime")},
                {"speed", schema::numberRange("Playback speed multiplier. Default 1.", 0.01, 10.0)},
                {"loop", schema::boolean("Clip previews: loop (default true)")},
                {"parameters", schema::anyObject("State previews: animator parameter values {name: number | boolean}")},
                {"stop", schema::boolean("End the previews of 'entity' / 'entities'")},
                {"stopAll", schema::boolean("End every preview (no other arguments)")}
            });
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"entity", "entities", "state", "clip", "normalizedTime", "playing", "speed",
                                              "loop", "parameters", "stop", "stopAll"}, "animation_preview");
                if (const std::optional<std::string> blocked = env::editBlocker("preview animations"))
                {
                    return ToolResult::error(*blocked + " In Play mode the animators run: read them with "
                                                        "animation_get_runtime_state.");
                }
                const ArgReader reader(args);
                auto& dispatcher = events::EventDispatcher::instance();

                if (reader.optBool("stopAll", false))
                {
                    for (auto it = args.begin(); it != args.end(); ++it)
                    {
                        if (it.key() != "stopAll" && !it->is_null())
                        {
                            throw ArgError("'stopAll' takes no other arguments (got '" + it.key() + "')");
                        }
                    }
                    authoring::StopAnimationEditPreviewCommand command; // an invalid entity stops every preview
                    const bool stopped = dispatcher.execute(command);
                    return reply({{"stoppedAll", true}, {"stopped", stopped}},
                                 stopped ? "Stopped every animation preview." : "No animation preview was running.");
                }

                const std::vector<services::EntityData> entities = previewEntities(reader);
                const bool hasState = reader.has("state");
                const bool hasClip = reader.has("clip");
                const bool playArguments = reader.has("normalizedTime") || reader.has("playing") || reader.has("speed") ||
                                           reader.has("loop") || reader.has("parameters");

                if (reader.optBool("stop", false))
                {
                    if (hasState || hasClip || playArguments)
                    {
                        throw ArgError("'stop' takes only 'entity' or 'entities'");
                    }
                    nlohmann::json stopped = nlohmann::json::array();
                    for (const services::EntityData& entity : entities)
                    {
                        authoring::StopAnimationEditPreviewCommand command;
                        command.entity = entity.handle;
                        stopped.push_back(nlohmann::json{{"entity", toId(entity.handle)},
                                                         {"stopped", dispatcher.execute(command)}});
                    }
                    return reply({{"stopped", std::move(stopped)}}, "Stopped the previews of " +
                                 std::to_string(entities.size()) + " entit" + (entities.size() == 1 ? "y." : "ies."));
                }

                const std::optional<fs::path> root = tryTerrainProjectRoot();
                if (!hasState && !hasClip)
                {
                    if (playArguments)
                    {
                        throw ArgError("pass 'state' (a state of the entity's animator) or 'clip' (a .vfAnim path)");
                    }
                    nlohmann::json previews = nlohmann::json::array();
                    std::size_t running = 0;
                    for (const services::EntityData& entity : entities)
                    {
                        authoring::GetAnimationEditPreviewQuery query;
                        query.entity = entity.handle;
                        const std::optional<services::AnimationEditPreviewStatus> status = dispatcher.query(query);
                        running += status.has_value() ? 1 : 0;
                        previews.push_back(nlohmann::json{{"entity", toId(entity.handle)},
                                                          {"preview", previewStatusJson(root, status)}});
                    }
                    return reply({{"previews", std::move(previews)}},
                                 std::to_string(running) + " of " + std::to_string(entities.size()) + " previewing.");
                }
                if (hasState && hasClip)
                {
                    throw ArgError("pass either 'state' or 'clip', not both");
                }
                if (hasState && reader.has("loop"))
                {
                    throw ArgError("'loop' applies to clip previews; a state uses its own loop setting");
                }
                if (hasClip && reader.has("parameters"))
                {
                    throw ArgError("'parameters' apply to state previews (they drive the animator's blend trees)");
                }

                services::AnimationEditPreviewRequest request;
                request.normalizedTime = static_cast<float>(reader.optNumberInRange("normalizedTime", 0.0, 1.0, 0.0));
                request.playing = reader.optBool("playing", true);
                request.speed = static_cast<float>(reader.optNumberInRange("speed", 0.01, 10.0, 1.0));
                if (hasClip)
                {
                    request.source = services::AnimationPreviewSource::Clip;
                    request.clipPath =
                        requireProjectFile(terrainProjectRoot(), reader.requireString("clip"), ".vfAnim", "clip").string();
                    request.loop = reader.optBool("loop", true);
                }
                else
                {
                    request.source = services::AnimationPreviewSource::State;
                    request.state = reader.requireString("state");
                    if (request.state.empty())
                    {
                        throw ArgError("argument 'state' must not be empty");
                    }
                }
                const nlohmann::json* parameters = reader.has("parameters") ? &reader.raw("parameters") : nullptr;
                if (parameters != nullptr && !parameters->is_object())
                {
                    throw ArgError("argument 'parameters' must be an object of parameter name -> value");
                }

                // Every entity is checked before any preview starts.
                std::vector<services::AnimationEditPreviewRequest> requests;
                for (const services::EntityData& entity : entities)
                {
                    const std::optional<services::MeshData> mesh = anim::meshData(entity.handle);
                    if (!mesh.has_value() || !mesh->animatorRef.isValid())
                    {
                        return ToolResult::error("Entity " + describe(entity) + " has no Animator: animation_preview "
                                                 "plays on an entity's own skeleton through its animator (component_add "
                                                 "{type:\"Animator\"} adds one)");
                    }
                    services::AnimationEditPreviewRequest entityRequest = request;
                    if (request.source == services::AnimationPreviewSource::State)
                    {
                        entityRequest.parameters = previewParameters(entity, *mesh, request.state, parameters);
                    }
                    requests.push_back(std::move(entityRequest));
                }

                nlohmann::json previews = nlohmann::json::array();
                std::size_t started = 0;
                std::string firstError;
                for (std::size_t i = 0; i < entities.size(); ++i)
                {
                    authoring::StartAnimationEditPreviewCommand command;
                    command.entity = entities[i].handle;
                    command.request = requests[i];
                    const services::AnimationEditPreviewResult result = dispatcher.execute(command);
                    nlohmann::json item{{"entity", toId(entities[i].handle)}, {"success", result.success}};
                    if (result.success)
                    {
                        ++started;
                        item["durationSeconds"] = env::floatJson(result.durationSeconds);
                        item["retargeted"] = result.retargeted;
                    }
                    else
                    {
                        item["error"] = result.error;
                        if (firstError.empty())
                        {
                            firstError = "entity " + describe(entities[i]) + ": " + result.error;
                        }
                    }
                    previews.push_back(std::move(item));
                }
                if (started == 0)
                {
                    return ToolResult::error("No preview started: " + firstError);
                }

                nlohmann::json out{
                    {"previews", std::move(previews)},
                    {"started", started},
                    {"source", hasClip ? "clip" : "state"},
                    {"hint", "Call viewport_screenshot to see the pose; animation_preview {stopAll:true} ends the "
                             "previews."}
                };
                if (hasClip)
                {
                    out["clip"] = projectRelativeUtf8(root, fs::path(request.clipPath));
                }
                else
                {
                    out["state"] = request.state;
                }
                return reply(std::move(out), "Previewing on " + std::to_string(started) + " of " +
                             std::to_string(entities.size()) + " entit" + (entities.size() == 1 ? "y" : "ies") +
                             "; call viewport_screenshot to see it.");
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // animation_get_runtime_state
        // ------------------------------------------------------------------

        void registerRuntimeState(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "animation_get_runtime_state";
            tool.title = "Get animator runtime state";
            tool.description =
                "Read an entity's live animator: {hasAnimator, playMode, playing, animator, layer, valid, currentState, "
                "currentStateId, previousState, previousStateId, stateTime (s), normalizedTime (base layer, 0-1), "
                "blending, blendProgress, activeTransition {id, from, to}, parameters:[{name, type, value}] (current "
                "values), conditions:[{transition, from, to, parameter, op, threshold, value, result}] (the current "
                "state's outgoing and Any State transitions), preview (edit mode: the running animation_preview)}. "
                "Works in Play mode: use it to check that a script's Animator::setFloat / setBool / setTrigger drive "
                "the expected transitions. 'layer' picks a layer of a layered animator (default 0).";
            tool.inputSchema = closedObject({
                {"entity", schema::entity("Entity with an Animator")},
                {"layer", schema::integerRange("Animator layer. Default 0 (the base layer).", 0, maxLayer)}
            }, {"entity"});
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"entity", "layer"}, "animation_get_runtime_state");
                const ArgReader reader(args);
                const services::EntityData entity = requireEntity(reader, "entity");
                const auto layer = static_cast<uint32_t>(reader.optIntInRange("layer", 0, maxLayer, 0));
                const services::EntityHandle handle = entity.handle;
                const std::optional<fs::path> root = tryTerrainProjectRoot();
                auto& dispatcher = events::EventDispatcher::instance();

                const bool playMode = dispatcher.query(events::editor::IsPlayModeQuery{});
                const std::optional<services::MeshData> mesh = anim::meshData(handle);
                const bool assigned = mesh.has_value() && mesh->animatorRef.isValid();
                const nlohmann::json animatorPath = assigned ? anim::assetPathJson(root, mesh->animatorRef) : nlohmann::json(nullptr);

                runtime::HasEntityAnimatorQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery))
                {
                    const std::string note = assigned
                        ? "The animator is assigned but not running yet: it starts with the next rendered frame, unless "
                          "its .vfAnimator or mesh failed to load (see logs_read)."
                        : "Entity " + describe(entity) + " has no Animator (component_add {type:\"Animator\"} adds one).";
                    return reply({{"entity", toId(handle)}, {"hasAnimator", false}, {"playMode", playMode},
                                  {"animator", animatorPath}, {"note", note}}, note);
                }

                runtime::GetAnimatorRuntimeDebugDataQuery debugQuery;
                debugQuery.entity = handle;
                debugQuery.layerIndex = layer;
                const services::AnimatorRuntimeDebugData debug = dispatcher.query(debugQuery);
                runtime::GetEntityAnimatorCurrentStateQuery stateQuery;
                stateQuery.entity = handle;
                const std::string baseState = dispatcher.query(stateQuery);
                runtime::GetEntityAnimatorNormalizedTimeQuery timeQuery;
                timeQuery.entity = handle;
                const float normalizedTime = dispatcher.query(timeQuery);
                runtime::IsEntityAnimatorPlayingQuery playingQuery;
                playingQuery.entity = handle;
                const bool playing = dispatcher.query(playingQuery);

                // State ids -> names through the animator file, when it can be read.
                std::optional<animator::AnimatorData> data;
                const std::string path = assigned ? mesh->animatorRef.resolve() : std::string();
                if (!path.empty())
                {
                    try
                    {
                        services::AnimatorAssetLoadResult loaded = anim::loadAnimator(path);
                        if (loaded.success)
                        {
                            data = std::move(loaded.data);
                        }
                    }
                    catch (const std::runtime_error&)
                    {
                        // No authoring handler: the ids are reported without names.
                    }
                }
                const animator::AnimatorGraph* graph = data.has_value() ? &anim::stateMachineLayer(*data, layer) : nullptr;
                const auto stateName = [graph](uint32_t id) -> nlohmann::json
                {
                    return graph == nullptr || id == 0 ? nlohmann::json(nullptr) : anim::stateRefJson(*graph, id);
                };
                const auto transitionEnds = [graph](uint32_t id) -> std::pair<nlohmann::json, nlohmann::json>
                {
                    if (graph != nullptr)
                    {
                        for (const animator::AnimatorTransition& transition : graph->transitions)
                        {
                            if (transition.id == id)
                            {
                                return {anim::stateRefJson(*graph, transition.sourceStateId),
                                        anim::stateRefJson(*graph, transition.targetStateId)};
                            }
                        }
                    }
                    return {nullptr, nullptr};
                };

                nlohmann::json parameters = nlohmann::json::array();
                for (const services::AnimatorParameterDebugInfo& parameter : debug.parameters)
                {
                    parameters.push_back(nlohmann::json{
                        {"name", parameter.name},
                        {"type", animator::parameterTypeToString(parameter.type)},
                        {"value", anim::parameterValueJson(parameter.currentValue)}
                    });
                }
                nlohmann::json conditions = nlohmann::json::array();
                for (const services::AnimatorConditionEval& condition : debug.conditionResults)
                {
                    const auto [from, to] = transitionEnds(condition.transitionId);
                    conditions.push_back(nlohmann::json{
                        {"transition", condition.transitionId},
                        {"from", from},
                        {"to", to},
                        {"parameter", condition.parameterName},
                        {"op", animator::comparisonOperatorToString(condition.op)},
                        {"threshold", anim::parameterValueJson(condition.threshold)},
                        {"value", anim::parameterValueJson(condition.currentValue)},
                        {"result", condition.result}
                    });
                }
                nlohmann::json activeTransition = nullptr;
                if (debug.isBlending && debug.activeTransitionId != 0)
                {
                    activeTransition = {{"id", debug.activeTransitionId}, {"from", stateName(debug.previousStateId)},
                                        {"to", stateName(debug.currentStateId)}};
                }
                // The runtime's own name for the base layer: the file on disk may have changed since it loaded.
                const nlohmann::json currentState =
                    layer == 0 && !baseState.empty() ? nlohmann::json(baseState) : stateName(debug.currentStateId);

                nlohmann::json out{
                    {"entity", toId(handle)},
                    {"hasAnimator", true},
                    {"playMode", playMode},
                    {"playing", playing},
                    {"animator", animatorPath},
                    {"layer", layer},
                    {"valid", debug.isValid},
                    {"currentState", currentState},
                    {"currentStateId", debug.currentStateId},
                    {"previousState", stateName(debug.previousStateId)},
                    {"previousStateId", debug.previousStateId},
                    {"stateTime", env::floatJson(debug.stateTime)},
                    {"normalizedTime", layer == 0 ? env::floatJson(normalizedTime) : nlohmann::json(nullptr)},
                    {"blending", debug.isBlending},
                    {"blendProgress", env::floatJson(debug.blendProgress)},
                    {"activeTransition", std::move(activeTransition)},
                    {"parameters", std::move(parameters)},
                    {"conditions", std::move(conditions)},
                    {"preview", nullptr}
                };
                if (!playMode)
                {
                    try
                    {
                        authoring::GetAnimationEditPreviewQuery query;
                        query.entity = handle;
                        out["preview"] = previewStatusJson(root, dispatcher.query(query));
                    }
                    catch (const std::runtime_error&)
                    {
                        // No preview handler in this build: report none.
                    }
                }

                std::string summary = "Entity " + describe(entity) + ": ";
                summary += currentState.is_string() ? "state " + currentState.get<std::string>() : std::string("no current state");
                summary += layer == 0 ? ", normalized time " + env::floatJson(normalizedTime).dump() : std::string();
                summary += debug.isBlending ? ", blending" : "";
                summary += playing ? ", playing." : ", not playing.";
                if (!debug.isValid)
                {
                    summary += " Layer " + std::to_string(layer) + " has no running state machine.";
                }
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }
    }

    void registerAnimationTools(ToolRegistry& registry, const ToolContext&)
    {
        registerListClips(registry);
        registerGetStateMachine(registry);
        registerSetStateMachine(registry);
        registerPreview(registry);
        registerRuntimeState(registry);
    }
}
