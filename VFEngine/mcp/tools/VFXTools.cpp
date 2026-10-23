#include "CoreTools.hpp"
#include "EnvironmentToolSupport.hpp"
#include "PathSandbox.hpp"
#include "SettingsTable.hpp"
#include "TerrainToolSupport.hpp"
#include "ToolHelpers.hpp"
#include "VFXToolSupport.hpp"
#include "../protocol/ArgReader.hpp"
#include "../undo/McpUndo.hpp"
#include "../undo/McpUndoCommands.hpp"

#include "asset/AssetRef.hpp"
#include "events/EventDispatcher.hpp"
#include "events/asset/AssetDatabaseEvents.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/vfx/VFXAssetSummaryEvents.hpp"
#include "events/vfx/VFXEditPreviewEvents.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// VK-1657 visual-effect tools: vfx_list_assets, vfx_add_emitter, vfx_set_params, vfx_play, vfx_stop. The
// component itself is the "VFX" binding of the component_* tools (VFXBinding.cpp) and every edit goes through
// it, so undo is the generic BuiltinComponentPresenceUndo / BuiltinComponentPatchUndo (one batch per call).
// The edit-mode preview belongs to the engine (services/events/vfx/VFXEditPreviewEvents.hpp) and is never
// undoable; the authored values come from the .vfVFX files (services/events/vfx/VFXAssetSummaryEvents.hpp).
namespace mcp::tools
{
    namespace
    {
        namespace fs = std::filesystem;
        namespace editpreview = services::events::vfxeditpreview;
        namespace vfxassets = services::events::vfxassets;

        constexpr std::chrono::milliseconds listAssetsTimeout{30000};
        constexpr int64_t maxListedAssets = 500;
        constexpr int64_t defaultListedAssets = 100;
        constexpr std::string_view resetAllKey = "all";

        constexpr const char* terrainHint =
            "Put the effect on an ordinary entity instead (entity_create; terrain_height_at gives the ground height).";
        constexpr const char* libraryHint =
            "The engine ships ready-made effects and combos in its assets/vfx/library folder: copy that folder into "
            "the project as assets/vfx/library, then call assets_rescan so the asset database registers them.";

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

        std::optional<services::VFXData> vfxData(const services::EntityHandle& entity)
        {
            events::scene::GetVFXDataQuery query;
            query.entity = entity;
            return events::EventDispatcher::instance().query(query);
        }

        // GetVFXAssetSummariesQuery: one summary per path, in order.
        std::vector<services::VFXAssetSummary> querySummaries(std::vector<std::string> paths)
        {
            if (paths.empty())
            {
                return {};
            }
            vfxassets::GetVFXAssetSummariesQuery query;
            query.paths = std::move(paths);
            std::vector<services::VFXAssetSummary> summaries = events::EventDispatcher::instance().query(query);
            if (summaries.size() != query.paths.size())
            {
                throw std::runtime_error("The VFX asset summary query answered " + std::to_string(summaries.size()) +
                                         " of " + std::to_string(query.paths.size()) + " assets");
            }
            return summaries;
        }

        // The authored summary of the effect at an engine path; nullopt for "", an unreadable file or a build
        // without the summary service (the authored values are then reported as null).
        std::optional<services::VFXAssetSummary> tryEffectSummary(const std::string& enginePath)
        {
            if (enginePath.empty())
            {
                return std::nullopt;
            }
            try
            {
                std::vector<services::VFXAssetSummary> summaries = querySummaries({enginePath});
                if (summaries.front().valid && summaries.front().effect.has_value())
                {
                    return std::move(summaries.front());
                }
            }
            catch (const std::runtime_error&)
            {
                // No summary service: report the overrides without their authored side.
            }
            return std::nullopt;
        }

        const services::VFXEffectSummary* effectOf(const std::optional<services::VFXAssetSummary>& summary)
        {
            return summary.has_value() && summary->effect.has_value() ? &*summary->effect : nullptr;
        }

        nlohmann::json previewJson(const std::optional<fs::path>& root, const services::VFXEditPreviewStatus& status)
        {
            return {
                {"playing", true},
                {"effect", vfxtool::projectPath(root, status.assetPath)},
                {"loop", status.loop},
                {"seconds", env::floatJson(status.seconds)},
                {"instanceId", status.instanceId}
            };
        }

        // The entity's running preview, null when it has none (or this build has no preview handler).
        nlohmann::json previewState(const std::optional<fs::path>& root, const services::EntityHandle& entity)
        {
            try
            {
                editpreview::GetVFXEditPreviewsQuery query;
                query.entity = entity;
                const std::vector<services::VFXEditPreviewStatus> previews = events::EventDispatcher::instance().query(query);
                if (!previews.empty())
                {
                    return previewJson(root, previews.front());
                }
            }
            catch (const std::runtime_error&)
            {
                // No preview handler in this build: report none.
            }
            return nullptr;
        }

        // How long to wait before a screenshot shows the effect.
        std::string fillHint(const std::optional<float>& lifetime)
        {
            const std::string wait = lifetime.has_value()
                ? "about " + env::floatJson(*lifetime).dump() + " s (the particle lifetime)"
                : "about one particle lifetime";
            return "A new preview starts empty and fills in over " + wait + ": wait that long, then call "
                   "viewport_screenshot (camera_focus {entity} frames the entity first). vfx_stop ends the preview.";
        }

        // A project .vfVFX named by the 'effect' argument: sandboxed to the project and existing.
        fs::path requireEffectFile(const fs::path& root, const std::string& path)
        {
            if (vfxtool::isComboPath(path))
            {
                throw ArgError("argument 'effect': '" + path + "' is a combo (.vfVFXSequence): combos are listed by "
                               "vfx_list_assets but cannot be attached in this version; attach one of its .vfVFX effects "
                               "instead (vfx_list_assets shows each step's effect)");
            }
            std::string error;
            const std::optional<fs::path> file = resolveInside(root, path, error, ".vfVFX");
            if (!file.has_value())
            {
                throw ArgError("argument 'effect': " + error + " (effects must lie inside the project; vfx_list_assets "
                               "lists them)");
            }
            std::error_code ec;
            if (!fs::is_regular_file(*file, ec))
            {
                throw ArgError("argument 'effect': no effect at '" + path + "' (vfx_list_assets lists the project's "
                               "effects)");
            }
            return *file;
        }

        // vfx_add_emitter / vfx_set_params 'params': the six table keys only.
        void checkParamsArgument(const nlohmann::json& params)
        {
            if (!params.is_object())
            {
                throw ArgError("argument 'params' must be an object of parameter -> value (null = as authored), got " +
                               env::detail::valueText(params));
            }
            for (auto it = params.begin(); it != params.end(); ++it)
            {
                if (vfxtool::findParam(it.key()) != nullptr)
                {
                    continue;
                }
                const std::string otherHint = " (the other overridable parameters are set with component_set "
                                              "{type:\"VFX\", data:{otherParams:{...}}})";
                if (it.key() == vfxtool::otherParamsKey)
                {
                    throw ArgError("'params.otherParams' is not one of the params keys (" + vfxtool::paramKeyList(false) +
                                   ")" + otherHint);
                }
                throw ArgError("unknown params key '" + it.key() + "'; valid keys: " + vfxtool::paramKeyList(false) +
                               otherHint);
            }
        }

        nlohmann::json paramSchema(const vfxtool::ParamSpec& spec)
        {
            nlohmann::json out;
            switch (spec.kind)
            {
            case vfxtool::ParamKind::Float:
                out = {{"type", nlohmann::json::array({"number", "null"})}, {"minimum", spec.min}, {"maximum", spec.max}};
                break;
            case vfxtool::ParamKind::Tint:
                out = {
                    {"type", nlohmann::json::array({"array", "null"})},
                    {"items", {{"type", "number"}, {"minimum", spec.min}, {"maximum", spec.max}}},
                    {"minItems", 3},
                    {"maxItems", 4}
                };
                break;
            case vfxtool::ParamKind::BlendMode:
            {
                nlohmann::json values = nlohmann::json::array();
                for (std::string_view name : vfxtool::blendModeNames)
                {
                    values.push_back(std::string(name));
                }
                values.push_back(nullptr);
                out = {{"type", nlohmann::json::array({"string", "null"})}, {"enum", std::move(values)}};
                break;
            }
            case vfxtool::ParamKind::Int:
                out = {
                    {"type", nlohmann::json::array({"integer", "null"})},
                    {"minimum", static_cast<int64_t>(spec.min)},
                    {"maximum", static_cast<int64_t>(spec.max)}
                };
                break;
            }
            std::string description(spec.help);
            if (spec.kind == vfxtool::ParamKind::BlendMode)
            {
                description = "Blend mode: " + description; // the help starts with a value name
            }
            else
            {
                description[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(description[0])));
            }
            if (spec.kind == vfxtool::ParamKind::Tint)
            {
                description += "; alpha 0-1, default 1";
            }
            out["description"] = description + ". null = as authored.";
            return out;
        }

        nlohmann::json paramsSchema(std::string description)
        {
            nlohmann::json properties = nlohmann::json::object();
            for (const vfxtool::ParamSpec& spec : vfxtool::paramSpecs)
            {
                properties[std::string(spec.key)] = paramSchema(spec);
            }
            nlohmann::json out = closedObject(std::move(properties));
            out["description"] = std::move(description);
            return out;
        }

        // vfx_set_params 'reset': table keys, "otherParams", or "all" (every one of them).
        struct ResetRequest
        {
            std::vector<std::string> keys;
            bool all = false;
        };

        ResetRequest resetRequest(const nlohmann::json& reset)
        {
            if (!reset.is_array() || reset.empty())
            {
                throw ArgError("argument 'reset' must be a non-empty array of parameter keys (or [\"all\"]), got " +
                               env::detail::valueText(reset));
            }
            ResetRequest request;
            for (std::size_t i = 0; i < reset.size(); ++i)
            {
                const std::string path = "reset[" + std::to_string(i) + "]";
                const std::string key = env::requireString(reset[i], path);
                if (key == resetAllKey)
                {
                    request.all = true;
                    continue;
                }
                if (key != vfxtool::otherParamsKey && vfxtool::findParam(key) == nullptr)
                {
                    throw ArgError("'" + path + "' must be one of " + vfxtool::paramKeyList(true) + ", all (got '" + key +
                                   "')");
                }
                if (std::find(request.keys.begin(), request.keys.end(), key) == request.keys.end())
                {
                    request.keys.push_back(key);
                }
            }
            if (request.all)
            {
                request.keys.clear();
                for (const vfxtool::ParamSpec& spec : vfxtool::paramSpecs)
                {
                    request.keys.emplace_back(spec.key);
                }
                request.keys.emplace_back(vfxtool::otherParamsKey);
            }
            return request;
        }

        // "size 1.6, color as authored" for the keys whose reported value differs.
        std::string changesText(const nlohmann::json& before, const nlohmann::json& after)
        {
            std::string text;
            for (auto it = after.begin(); it != after.end(); ++it)
            {
                const auto previous = before.find(it.key());
                if (previous != before.end() && *previous == *it)
                {
                    continue;
                }
                text += text.empty() ? "" : ", ";
                text += it.key() + " " + (it->is_null() ? std::string("as authored") : env::detail::valueText(*it));
            }
            return text;
        }

        // ------------------------------------------------------------------
        // vfx_list_assets
        // ------------------------------------------------------------------

        void registerListAssets(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "vfx_list_assets";
            tool.title = "List visual effects";
            tool.description =
                "List the project's visual effects from the asset database: effects (.vfVFX) and combos "
                "(.vfVFXSequence), files inside the project only. A .vfVFX IS the effect's node graph: one emitter "
                "(spawn rate, lifetime, size, colour, blend mode, ...) and its modifiers; attach one to an entity with "
                "vfx_add_emitter, then tune it per entity with vfx_set_params. A combo times several effects (and "
                "sounds / script cues) on one timeline: it is listed but cannot be attached to an entity in this "
                "version; a script spawns it with VFX::spawnCombo. Scripts react to effects through IVFXEventListener "
                "(particle events) and IVFXComboCueListener (combo cues): see vf://docs/mtype-api. When nothing is "
                "listed: " + std::string(libraryHint) + " Values are as authored in the files. Returns {effects:"
                "[{path, name, valid, error?, spawnRate, lifetime, size, startSpeed, startColor, looping, burstCount, "
                "colorOverLifetime, birthColor, deathColor, blendMode, sortOrder, renderMode, emitsLight, texture, events:"
                "[{type, notify, subEffect}], usedBy?:[{entity, name}]}], combos:[{path, name, valid, error?, steps:"
                "[{label, kind, effect, startTime, cue, loop}], cues, playbackRate}], total, returned, truncated}, "
                "sorted by path; usedBy (effects only) lists the entities whose VFX component plays the effect. Works "
                "in Play mode.";
            tool.inputSchema = closedObject({
                {"kind", schema::enumString("effects (.vfVFX), combos (.vfVFXSequence) or all. Default all.",
                                            {"effects", "combos", "all"})},
                {"contains", schema::string("Case-insensitive substring filter on the project-relative path, e.g. "
                                            "\"fire\"")},
                {"max", schema::integerRange("Maximum assets returned. Default 100.", 1, maxListedAssets)},
                {"usedBy", schema::boolean("Also list the entities whose VFX component plays each effect. Default "
                                           "false.")}
            });
            tool.readOnly = true;
            tool.timeout = listAssetsTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"kind", "contains", "max", "usedBy"}, "vfx_list_assets");
                const ArgReader reader(args);
                const std::string kind = reader.optEnum("kind", {"effects", "combos", "all"}, "all");
                const std::string needle = lowercase(reader.optString("contains"));
                const auto max = static_cast<std::size_t>(reader.optIntInRange("max", 1, maxListedAssets, defaultListedAssets));
                const bool usedBy = reader.optBool("usedBy", false);
                const fs::path root = terrainProjectRoot();
                auto& dispatcher = events::EventDispatcher::instance();

                struct Listed
                {
                    std::string relative;
                    std::string path; // engine path, as the asset database stores it
                    bool combo = false;
                    asset::AssetGUID guid;
                };
                std::vector<std::pair<resource::AssetType, bool>> types; // asset type, is a combo
                if (kind != "combos")
                {
                    types.emplace_back(resource::AssetType::VFX, false);
                }
                if (kind != "effects")
                {
                    types.emplace_back(resource::AssetType::VFXSequence, true);
                }
                std::vector<Listed> listed;
                for (const auto& [type, combo] : types)
                {
                    events::assetdb::GetAssetsByTypeQuery query;
                    query.type = type;
                    for (const events::assetdb::AssetEntryData& entry : dispatcher.query(query))
                    {
                        std::optional<std::string> relative = vfxtool::insideProjectPath(root, entry.path);
                        if (!relative.has_value())
                        {
                            continue;
                        }
                        if (!needle.empty() && lowercase(*relative).find(needle) == std::string::npos)
                        {
                            continue;
                        }
                        listed.push_back({std::move(*relative), entry.path, combo, entry.guid});
                    }
                }
                std::sort(listed.begin(), listed.end(),
                          [](const Listed& a, const Listed& b) { return a.relative < b.relative; });

                const std::size_t total = listed.size();
                const std::size_t returned = std::min(total, max);
                std::vector<std::string> paths;
                bool pageHasEffects = false;
                bool pageHasCombos = false;
                for (std::size_t i = 0; i < returned; ++i)
                {
                    paths.push_back(listed[i].path);
                    pageHasEffects = pageHasEffects || !listed[i].combo;
                    pageHasCombos = pageHasCombos || listed[i].combo;
                }
                const std::vector<services::VFXAssetSummary> summaries = querySummaries(std::move(paths));

                // Which entities play which effect: one pass over the VFX components.
                std::vector<std::pair<asset::AssetGUID, services::EntityHandle>> players;
                if (usedBy && pageHasEffects)
                {
                    events::scene::GetEntitiesWithComponentQuery query;
                    query.componentType = services::ComponentTypeId::VFX;
                    for (const services::EntityHandle& entity : dispatcher.query(query))
                    {
                        const std::optional<services::VFXData> data = vfxData(entity);
                        if (data.has_value() && data->vfxRef.isValid())
                        {
                            players.emplace_back(data->vfxRef.getGUID(), entity);
                        }
                    }
                }

                nlohmann::json effects = nlohmann::json::array();
                nlohmann::json combos = nlohmann::json::array();
                for (std::size_t i = 0; i < returned; ++i)
                {
                    // Reported in the listing's own spelling, whatever path the summary echoes.
                    if (listed[i].combo)
                    {
                        nlohmann::json item = vfxtool::comboJson(summaries[i], root);
                        item["path"] = listed[i].relative;
                        combos.push_back(std::move(item));
                        continue;
                    }
                    nlohmann::json item = vfxtool::effectJson(summaries[i], root);
                    item["path"] = listed[i].relative;
                    if (usedBy)
                    {
                        nlohmann::json users = nlohmann::json::array();
                        for (const auto& [guid, entity] : players)
                        {
                            if (guid == listed[i].guid)
                            {
                                const std::optional<services::EntityData> data = findEntity(entity);
                                users.push_back(nlohmann::json{
                                    {"entity", toId(entity)},
                                    {"name", data.has_value() ? nlohmann::json(data->name) : nlohmann::json(nullptr)}
                                });
                            }
                        }
                        item["usedBy"] = std::move(users);
                    }
                    effects.push_back(std::move(item));
                }

                const std::size_t effectCount = effects.size();
                const std::size_t comboCount = combos.size();
                nlohmann::json out{
                    {"effects", std::move(effects)},
                    {"combos", std::move(combos)},
                    {"total", total},
                    {"returned", returned},
                    {"truncated", total > returned}
                };
                if (total == 0)
                {
                    out["hint"] = libraryHint;
                }
                if (usedBy && pageHasCombos)
                {
                    out["note"] = "usedBy covers effects only: combos are played by VFXSequence components, which these "
                                  "tools do not report yet.";
                }

                std::string summary = std::to_string(effectCount) + " effect(s) and " + std::to_string(comboCount) +
                                      " combo(s) listed";
                summary += needle.empty() ? "." : " matching \"" + reader.optString("contains") + "\".";
                if (total > returned)
                {
                    summary += " Showing the first " + std::to_string(returned) + " of " + std::to_string(total) + ".";
                }
                if (total == 0)
                {
                    summary += std::string(" ") + libraryHint;
                }
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // vfx_add_emitter
        // ------------------------------------------------------------------

        void registerAddEmitter(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "vfx_add_emitter";
            tool.title = "Add VFX emitter";
            tool.description =
                "Add a VFX component to an entity: it plays the .vfVFX 'effect' (a project-relative path from "
                "vfx_list_assets) at the entity's world transform, with per-entity 'params' - the vfx_set_params keys: " +
                vfxtool::paramsHelp(false) + ". The .vfVFX itself is never written. autoPlay (default true) starts it "
                "when Play mode starts; loop (default true) repeats it. preview:true also starts the edit-mode preview "
                "(vfx_play). Refused when the entity already has a VFX component (change it with vfx_set_params or "
                "component_set {type:\"VFX\"}), for the scene root and terrain entities, for combos (.vfVFXSequence), in "
                "Play mode and while a scene load is pending. One undo step (it removes the component again). Returns "
                "{entity, data (the component as component_get {type:\"VFX\"} reports it), effect (the authored "
                "values, as vfx_list_assets lists them), preview, hint}.";
            tool.inputSchema = closedObject({
                {"entity", schema::entity("Entity that plays the effect; its world transform places it")},
                {"effect", schema::string(".vfVFX path inside the project, e.g. \"assets/vfx/library/<name>.vfVFX\" "
                                          "(vfx_list_assets)")},
                {"autoPlay", schema::boolean("Play when Play mode starts. Default true.")},
                {"loop", schema::boolean("Repeat the effect. Default true.")},
                {"params", paramsSchema("Per-entity parameters, as in vfx_set_params (omitted or null = as authored)")},
                {"preview", schema::boolean("Also start the edit-mode preview (vfx_play). Default false.")}
            }, {"entity", "effect"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"entity", "effect", "autoPlay", "loop", "params", "preview"},
                                       "vfx_add_emitter");
                if (const std::optional<std::string> blocked = env::editBlocker("add a VFX emitter"))
                {
                    return ToolResult::error(*blocked);
                }
                const ArgReader reader(args);
                const services::EntityData entity = requireEntity(reader, "entity");
                rejectRoot(entity, "add a VFX emitter to");
                rejectTerrain(entity, "add a VFX emitter to", terrainHint);
                const bool autoPlay = reader.optBool("autoPlay", true);
                const bool loop = reader.optBool("loop", true);
                const bool preview = reader.optBool("preview", false);
                const nlohmann::json params = reader.has("params") ? reader.raw("params") : nlohmann::json::object();
                checkParamsArgument(params);
                // Every value is checked before the component exists.
                vfxtool::patchOverrides({}, params, "params");

                const fs::path root = terrainProjectRoot();
                const fs::path file = requireEffectFile(root, reader.requireString("effect"));
                const std::string effectText = projectRelativeUtf8(root, file);

                if (vfxData(entity.handle).has_value())
                {
                    return ToolResult::error("Entity " + describe(entity) + " already has a VFX component: change its "
                                             "parameters with vfx_set_params, or its effect, autoPlay and loop with "
                                             "component_set {type:\"VFX\", data:{vfxRef, autoPlay, loop}}; component_remove "
                                             "{type:\"VFX\"} removes it");
                }
                const std::vector<services::VFXAssetSummary> found = querySummaries({file.string()});
                const services::VFXAssetSummary& summary = found.front();
                if (!summary.valid || !summary.effect.has_value())
                {
                    return ToolResult::error("Cannot read the effect '" + effectText + "': " +
                                             (summary.error.empty() ? std::string("not a .vfVFX effect") : summary.error));
                }

                nlohmann::json patch = params;
                patch["vfxRef"] = effectText;
                patch["autoPlay"] = autoPlay;
                patch["loop"] = loop;

                if (!addBuiltinComponent("VFX", entity.handle))
                {
                    return ToolResult::error("AddVFXComponent failed for entity " + describe(entity));
                }
                std::optional<nlohmann::json> fields;
                try
                {
                    fields = patchBuiltinComponent("VFX", entity.handle, patch);
                    if (!fields.has_value())
                    {
                        throw std::runtime_error("Entity " + describe(entity) + " lost its new VFX component");
                    }
                }
                catch (...)
                {
                    // Don't leave a half-applied, default-valued component behind.
                    removeBuiltinComponent("VFX", entity.handle);
                    throw;
                }

                undo::UndoRecorder recorder("MCP: Add VFX emitter (" + entity.name + ")");
                recorder.add(std::make_unique<undo::BuiltinComponentPresenceUndo>(toId(entity.handle), "VFX", *fields, true));
                recorder.push();

                // The add is done (and undoable) from here on: a failed preview is reported, never thrown.
                nlohmann::json previewOut = nullptr;
                if (preview)
                {
                    try
                    {
                        editpreview::StartVFXEditPreviewCommand command;
                        command.entity = entity.handle;
                        const services::VFXEditPreviewResult result = events::EventDispatcher::instance().execute(command);
                        previewOut = result.started ? previewJson(root, result.status)
                                                    : nlohmann::json{{"playing", false}, {"error", result.error}};
                    }
                    catch (const std::exception& e)
                    {
                        previewOut = {{"playing", false}, {"error", e.what()}};
                    }
                }

                const std::optional<services::VFXData> data = vfxData(entity.handle);
                const float lifetime = vfxtool::effectiveLifetime(
                    *summary.effect, data.has_value() ? data->overrides : std::vector<vfx::VFXParamOverride>{});
                const bool previewing = previewOut.is_object() && previewOut.value("playing", false);
                std::string hint = previewing
                    ? fillHint(lifetime)
                    : "vfx_play {entity} previews it in the editor. " + fillHint(lifetime);
                hint += " vfx_set_params tunes size, color, spawnRate and the others.";

                nlohmann::json out{
                    {"entity", toId(entity.handle)},
                    {"data", *fields},
                    {"effect", vfxtool::effectJson(summary, root)},
                    {"preview", previewOut},
                    {"hint", hint}
                };
                std::string text = "Added the VFX emitter '" + effectText + "' to entity " + describe(entity) + ".";
                if (previewing)
                {
                    text += " Previewing it now.";
                }
                else if (preview)
                {
                    text += " The preview did not start: " + previewOut.value("error", std::string("unknown error"));
                }
                return reply(std::move(out), text + " " + hint);
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // vfx_set_params
        // ------------------------------------------------------------------

        void registerSetParams(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "vfx_set_params";
            tool.title = "Set VFX parameters";
            tool.description =
                "Change the per-entity parameters of an entity's VFX component (the .vfVFX is never written): params "
                "{" + vfxtool::paramsHelp(false) + "}; null returns a parameter to its authored value. color is a TINT: "
                "an effect with a colour-over-lifetime gradient replaces its start colour every frame, so the tint is "
                "what recolours it (orange: [1, 0.6, 0.25]). reset: [keys] or [\"all\"] (the six and otherParams) "
                "returns them to the authored values; a key may not be both in params and in reset. Updates are strict "
                "and all-or-nothing; a running preview follows at once. One undo step, none when nothing changes "
                "(changed:false). Refused in Play mode and while a scene load is pending. Returns {entity, changed, "
                "effect, params:{<key>:{override, authored, effective}}, otherParams, birthColor:{authored, effective, "
                "source}, preview}: birthColor is the colour particles are born with (a colour-over-lifetime gradient's "
                "first colour, else the start colour, times the tint). The other overridable parameters (startSpeed, "
                "gravityStrength, ...): component_set {type:\"VFX\", data:{otherParams}}.";
            nlohmann::json resetKeys = nlohmann::json::array();
            for (const vfxtool::ParamSpec& spec : vfxtool::paramSpecs)
            {
                resetKeys.push_back(std::string(spec.key));
            }
            resetKeys.push_back(std::string(vfxtool::otherParamsKey));
            resetKeys.push_back(std::string(resetAllKey));
            nlohmann::json reset = schema::array({{"type", "string"}, {"enum", std::move(resetKeys)}},
                                                 "Parameters to return to their authored values; [\"all\"] = every one");
            reset["minItems"] = 1;
            tool.inputSchema = closedObject({
                {"entity", schema::entity("Entity with a VFX component")},
                {"params", paramsSchema("Values to set; null = back to the authored value")},
                {"reset", std::move(reset)}
            }, {"entity"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"entity", "params", "reset"}, "vfx_set_params");
                if (const std::optional<std::string> blocked = env::editBlocker("set VFX parameters"))
                {
                    return ToolResult::error(*blocked);
                }
                const ArgReader reader(args);
                const services::EntityData entity = requireEntity(reader, "entity");
                if (!reader.has("params") && !reader.has("reset"))
                {
                    throw ArgError("pass 'params' (values to set; null returns a parameter to its authored value) and/or "
                                   "'reset' (parameters to return to their authored values, or [\"all\"])");
                }

                nlohmann::json patch = nlohmann::json::object();
                ResetRequest reset;
                if (reader.has("reset"))
                {
                    reset = resetRequest(reader.raw("reset"));
                    for (const std::string& key : reset.keys)
                    {
                        patch[key] = key == vfxtool::otherParamsKey ? nlohmann::json::object() : nlohmann::json(nullptr);
                    }
                }
                const nlohmann::json* params = reader.has("params") ? &reader.raw("params") : nullptr;
                if (params != nullptr)
                {
                    checkParamsArgument(*params);
                    for (auto it = params->begin(); it != params->end(); ++it)
                    {
                        if (patch.contains(it.key()))
                        {
                            throw ArgError("'" + it.key() + "' is both in 'params' and in 'reset'" +
                                           (reset.all ? std::string(": reset [\"all\"] covers every parameter - to reset the "
                                                                    "others and set this one, pass the others as null in "
                                                                    "'params' instead")
                                                      : std::string()));
                        }
                        patch[it.key()] = it.value();
                    }
                }

                const std::optional<nlohmann::json> before = getBuiltinComponent("VFX", entity.handle);
                const std::optional<services::VFXData> current = vfxData(entity.handle);
                if (!before.has_value() || !current.has_value())
                {
                    return ToolResult::error("Entity " + describe(entity) + " has no VFX component; add one with "
                                             "vfx_add_emitter");
                }
                if (params != nullptr)
                {
                    // Checked with the argument's own names ('params.size') before anything is written.
                    vfxtool::patchOverrides(current->overrides, *params, "params");
                }
                const std::optional<nlohmann::json> after = patchBuiltinComponent("VFX", entity.handle, patch);
                if (!after.has_value())
                {
                    return ToolResult::error("Entity " + describe(entity) + " has no VFX component; add one with "
                                             "vfx_add_emitter");
                }

                const bool changed = *before != *after;
                if (changed)
                {
                    undo::UndoRecorder recorder("MCP: Set VFX parameters (" + entity.name + ")");
                    recorder.add(std::make_unique<undo::BuiltinComponentPatchUndo>(toId(entity.handle), "VFX", *before,
                                                                                   *after));
                    recorder.push();
                }

                const std::optional<services::VFXData> data = vfxData(entity.handle);
                const std::vector<vfx::VFXParamOverride> overrides =
                    data.has_value() ? data->overrides : std::vector<vfx::VFXParamOverride>{};
                const std::optional<services::VFXAssetSummary> summary =
                    data.has_value() && data->vfxRef.isValid() ? tryEffectSummary(data->vfxRef.resolve())
                                                               : std::optional<services::VFXAssetSummary>();
                const services::VFXEffectSummary* effect = effectOf(summary);
                const std::optional<fs::path> root = tryTerrainProjectRoot();
                nlohmann::json preview = previewState(root, entity.handle);

                nlohmann::json out{
                    {"entity", toId(entity.handle)},
                    {"changed", changed},
                    {"effect", after->at("vfxRef")},
                    {"params", vfxtool::paramsReportJson(effect, overrides)},
                    {"otherParams", after->at(std::string(vfxtool::otherParamsKey))},
                    {"birthColor", vfxtool::birthColorJson(effect, overrides)},
                    {"preview", preview}
                };
                if (effect == nullptr)
                {
                    out["note"] = "The authored values are unknown: the component's effect cannot be read (vfx_list_assets "
                                  "reports why).";
                }

                std::string text = changed
                    ? "Updated the VFX parameters of entity " + describe(entity) + ": " + changesText(*before, *after) + "."
                    : "Unchanged: entity " + describe(entity) + " already has these VFX parameters.";
                text += preview.is_null() ? " vfx_play previews the effect in the editor."
                                          : " The running preview follows on the next frame (a removed parameter restarts "
                                            "it).";
                return reply(std::move(out), text);
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // vfx_play / vfx_stop
        // ------------------------------------------------------------------

        void registerPlay(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "vfx_play";
            tool.title = "Preview VFX";
            tool.description =
                "Preview an entity's VFX component in edit mode: the editor viewport plays its effect with the entity's "
                "per-entity parameters (Play mode is not needed); restarts a running preview. The preview follows the "
                "entity's transform and every change to the component, and ends with vfx_stop, play_start, a scene load "
                "or clear, deleting the entity or removing the component. A new preview starts empty and fills in over "
                "about one particle lifetime: wait that long, then call viewport_screenshot. Not undoable (nothing is "
                "saved). Refused in Play mode, where the component plays by itself when autoPlay is true (else a "
                "script's VFX::play starts it). Returns {entity, effect, playing, loop, lifetime (the particle lifetime "
                "in seconds, null when the effect cannot be read), preview, hint}.";
            tool.inputSchema = closedObject({
                {"entity", schema::entity("Entity with a VFX component")}
            }, {"entity"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"entity"}, "vfx_play");
                if (const std::optional<std::string> blocked = env::editBlocker("preview an effect"))
                {
                    return ToolResult::error(*blocked + " In Play mode a VFX component plays by itself when autoPlay is "
                                                        "true; otherwise a script starts it with VFX::play(entityId).");
                }
                const ArgReader reader(args);
                const services::EntityData entity = requireEntity(reader, "entity");
                const std::optional<services::VFXData> data = vfxData(entity.handle);
                if (!data.has_value())
                {
                    return ToolResult::error("Entity " + describe(entity) + " has no VFX component; add one with "
                                             "vfx_add_emitter");
                }
                if (!data->vfxRef.isValid())
                {
                    return ToolResult::error("The VFX component of entity " + describe(entity) + " has no effect: set one "
                                             "with component_set {type:\"VFX\", data:{vfxRef}}");
                }

                editpreview::StartVFXEditPreviewCommand command;
                command.entity = entity.handle;
                const services::VFXEditPreviewResult result = events::EventDispatcher::instance().execute(command);
                if (!result.started)
                {
                    return ToolResult::error("Cannot preview the effect of entity " + describe(entity) + ": " +
                                             (result.error.empty() ? std::string("the engine refused it") : result.error));
                }

                const std::optional<fs::path> root = tryTerrainProjectRoot();
                const std::string& enginePath =
                    result.status.assetPath.empty() ? data->vfxRef.resolve() : result.status.assetPath;
                const std::optional<services::VFXAssetSummary> summary = tryEffectSummary(enginePath);
                const services::VFXEffectSummary* effect = effectOf(summary);
                const std::optional<float> lifetime =
                    effect != nullptr ? std::optional<float>(vfxtool::effectiveLifetime(*effect, data->overrides))
                                      : std::nullopt;
                const std::string hint = fillHint(lifetime);

                nlohmann::json out{
                    {"entity", toId(entity.handle)},
                    {"effect", vfxtool::projectPath(root, enginePath)},
                    {"playing", true},
                    {"loop", result.status.loop},
                    {"lifetime", lifetime.has_value() ? env::floatJson(*lifetime) : nlohmann::json(nullptr)},
                    {"preview", previewJson(root, result.status)},
                    {"hint", hint}
                };
                return reply(std::move(out), "Previewing '" + vfxtool::projectPath(root, enginePath) + "' on entity " +
                             describe(entity) + ". " + hint);
            };
            registry.add(std::move(tool));
        }

        void registerStop(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "vfx_stop";
            tool.title = "Stop VFX preview";
            tool.description =
                "End edit-mode VFX previews (vfx_play): {entity} stops that entity's, {all:true} every one. Works in any "
                "mode (play_start already ended them). Not undoable. Returns {stopped (how many)}.";
            tool.inputSchema = closedObject({
                {"entity", schema::entity("Entity whose preview to stop")},
                {"all", schema::boolean("true: stop every preview (instead of 'entity')")}
            });
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"entity", "all"}, "vfx_stop");
                const ArgReader reader(args);
                const bool all = reader.optBool("all", false);
                if (all == reader.has("entity"))
                {
                    throw ArgError("pass exactly one of 'entity' or all:true");
                }

                editpreview::StopVFXEditPreviewCommand command; // an invalid entity stops every preview
                std::optional<services::EntityData> entity;
                if (!all)
                {
                    entity = requireEntity(reader, "entity");
                    command.entity = entity->handle;
                }
                const uint32_t stopped = events::EventDispatcher::instance().execute(command);

                nlohmann::json out{{"stopped", stopped}};
                if (entity.has_value())
                {
                    out["entity"] = toId(entity->handle);
                }
                else
                {
                    out["all"] = true;
                }
                const std::string text = stopped == 0
                    ? std::string("No VFX preview was running") + (entity.has_value() ? " on entity " + describe(*entity) : "") + "."
                    : "Stopped " + std::to_string(stopped) + " VFX preview(s).";
                return reply(std::move(out), text);
            };
            registry.add(std::move(tool));
        }
    }

    void registerVFXTools(ToolRegistry& registry, const ToolContext&)
    {
        registerListAssets(registry);
        registerAddEmitter(registry);
        registerSetParams(registry);
        registerPlay(registry);
        registerStop(registry);
    }
}
