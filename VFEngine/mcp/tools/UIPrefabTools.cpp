#include "CoreTools.hpp"
#include "EnvironmentToolSupport.hpp"
#include "PathSandbox.hpp"
#include "SettingsTable.hpp"
#include "TerrainToolSupport.hpp"
#include "ToolHelpers.hpp"
#include "UIToolSupport.hpp"
#include "UIWidgets.hpp"
#include "../protocol/ArgReader.hpp"
#include "../undo/McpUndo.hpp"
#include "../undo/McpUndoCommands.hpp"

#include "data/UIAuthoringData.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/project/ResourceEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "events/scene/SceneSettingsEvents.hpp"
#include "events/ui/UIAuthoringEvents.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

// VK-1659 UI tools, part 3: ui_save_prefab (a UI subtree -> .vfPrefab, e.g. a ListView row template),
// ui_instantiate_prefab (one undo step) and ui_click (Play mode: a real click's notifications, so the agent can
// check its button / checkbox wiring). Prefab files live inside the project.
namespace mcp::tools::uitool
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr std::chrono::milliseconds prefabTimeout{30000};
        constexpr std::string_view prefabExtension = ".vfPrefab";

        fs::path requirePrefabPath(const fs::path& root, const ArgReader& reader)
        {
            std::string error;
            const std::optional<fs::path> file = resolveInside(root, reader.requireString("path"), error, prefabExtension);
            if (!file.has_value())
            {
                throw ArgError("argument 'path': " + error + " (a project-relative .vfPrefab path, e.g. "
                               "\"ui/HealthRow.vfPrefab\")");
            }
            return *file;
        }

        // Every live entity below `root` that is an engine-managed list row (UIListItemComponent).
        std::size_t countListItems(const services::EntityData& root)
        {
            std::size_t count = 0;
            std::unordered_set<uint64_t> seen{root.handle.id};
            std::vector<services::EntityHandle> pending = root.children;
            while (!pending.empty())
            {
                const services::EntityHandle handle = pending.back();
                pending.pop_back();
                if (!seen.insert(handle.id).second)
                {
                    continue;
                }
                const std::optional<services::EntityData> entity = findEntity(handle);
                if (!entity.has_value())
                {
                    continue;
                }
                if (isListItem(handle))
                {
                    ++count;
                }
                pending.insert(pending.end(), entity->children.begin(), entity->children.end());
            }
            return count;
        }

        // ------------------------------------------------------------------
        // ui_save_prefab
        // ------------------------------------------------------------------

        void registerSavePrefab(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "ui_save_prefab";
            tool.title = "Save UI prefab";
            tool.description =
                "Save a canvas or widget and everything below it as a .vfPrefab inside the project (path project-relative, "
                "e.g. \"ui/ScoreRow.vfPrefab\"; folders are created). Use it to make a ListView row template "
                "(listView.itemTemplateRef) or a reusable panel (ui_instantiate_prefab). Refuses an existing file unless "
                "overwrite:true. The asset database registers the file, and the entity becomes linked to it (a prefab "
                "instance). Not undoable (it writes a file). Refused for ListView rows, in Play mode and while a scene "
                "load is pending. Returns {entity, path, overwritten}.";
            tool.inputSchema = closedObject({
                {"entity", schema::entity("The canvas or widget to save (with everything below it)")},
                {"path", schema::string("Project-relative .vfPrefab path")},
                {"overwrite", schema::boolean("Replace an existing file. Default false.")}
            }, {"entity", "path"});
            tool.destructive = true;
            tool.timeout = prefabTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"entity", "path", "overwrite"}, "ui_save_prefab");
                if (const std::optional<std::string> blocked = env::editBlocker("save a UI prefab"))
                {
                    return ToolResult::error(*blocked);
                }
                const ArgReader reader(args);
                const services::EntityData entity = requireUIEntity(args, "entity");
                rejectRoot(entity, "save as a prefab");
                if (isListItem(entity.handle))
                {
                    throw std::runtime_error("Entity " + describe(entity) + " is a ListView row (engine-managed): save the "
                                             "widget the rows are made from instead.");
                }
                const bool overwrite = reader.optBool("overwrite", false);
                const fs::path root = terrainProjectRoot();
                const fs::path file = requirePrefabPath(root, reader);
                const std::string relative = projectRelativeUtf8(root, file);

                std::error_code ec;
                if (fs::is_directory(file, ec))
                {
                    return ToolResult::error("'" + relative + "' is a folder");
                }
                const bool exists = fs::exists(file, ec);
                if (exists && !overwrite)
                {
                    return ToolResult::error("'" + relative + "' already exists: pass overwrite:true to replace it, or "
                                             "choose another path");
                }
                fs::create_directories(file.parent_path(), ec);
                if (ec)
                {
                    return ToolResult::error("Cannot create the folder of '" + relative + "': " + ec.message());
                }

                auto& dispatcher = events::EventDispatcher::instance();
                events::scene::SavePrefabCommand save;
                save.entity = entity.handle;
                save.filePath = file.string();
                if (!dispatcher.execute(save))
                {
                    return ToolResult::error("Saving '" + relative + "' failed (logs_read has the reason)");
                }
                // What the UI Layer Builder publishes after a save: the asset database registers the .vfPrefab (GUID).
                events::resource::AssetSavedNotification saved;
                saved.filePath = file.string();
                dispatcher.publish(saved);

                nlohmann::json out{
                    {"entity", toId(entity.handle)},
                    {"path", relative},
                    {"overwritten", exists},
                    {"linked", true},
                    {"note", "Not undoable. The entity is now an instance of this prefab. Use the path as "
                             "listView.itemTemplateRef or with ui_instantiate_prefab."}
                };
                return reply(std::move(out), std::string(exists ? "Overwrote" : "Saved") + " '" + relative + "' from " +
                                                 describe(entity) + ".");
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // ui_instantiate_prefab
        // ------------------------------------------------------------------

        void registerInstantiatePrefab(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "ui_instantiate_prefab";
            tool.title = "Instantiate UI prefab";
            tool.description =
                "Add a copy of a UI .vfPrefab (project-relative path) to the scene. A canvas prefab goes to the scene "
                "root or under a non-UI 'parent'; a widget prefab needs 'parent' (a canvas or a widget inside one). "
                "Optional 'name' renames its root and 'placement' {preset, size, margin, offset, relativeTo} re-places a "
                "widget prefab (everything below it moves along). ListView rows are built from their templates. Refused "
                "in Play mode, World mode and while a scene load is pending. One undo step. Returns {entity, name, path, "
                "parent, canvasId, listItemsBuilt, widget (as ui_get reports it)}.";
            tool.inputSchema = closedObject({
                {"path", schema::string("Project-relative .vfPrefab path")},
                {"parent", schema::entity("Where it goes: a canvas or a widget for a widget prefab; omit for a canvas "
                                          "prefab (scene root)")},
                {"name", schema::string("New name for its root")},
                {"placement", placementSchema("Re-place a widget prefab inside its parent (reference px)")}
            }, {"path"});
            tool.timeout = prefabTimeout;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"path", "parent", "name", "placement"}, "ui_instantiate_prefab");
                if (const std::optional<std::string> blocked = env::editBlocker("instantiate a UI prefab"))
                {
                    return ToolResult::error(*blocked);
                }
                if (isWorldModeActive())
                {
                    return ToolResult::error(worldModeRefusal);
                }
                const ArgReader reader(args);
                const fs::path root = terrainProjectRoot();
                const fs::path file = requirePrefabPath(root, reader);
                const std::string relative = projectRelativeUtf8(root, file);
                std::error_code ec;
                if (!fs::is_regular_file(file, ec))
                {
                    return ToolResult::error("'" + relative + "' does not exist (ui_save_prefab makes one)");
                }

                std::optional<services::EntityData> parent;
                if (reader.has("parent"))
                {
                    parent = requireEntity(reader, "parent");
                    rejectPreviewSandbox(*parent, "parent");
                    rejectTerrain(*parent, "instantiate a UI prefab under", "Use a canvas or a widget as the parent.");
                    rejectOcean(*parent, "instantiate a UI prefab under", "Use a canvas or a widget as the parent.");
                    if (isListItem(parent->handle))
                    {
                        throw std::runtime_error("Entity " + describe(*parent) + " is a ListView row (engine-managed): "
                                                 "pick its ListView's canvas or another widget.");
                    }
                    if (hasSection(parent->handle, Section::ListView))
                    {
                        throw std::runtime_error("Entity " + describe(*parent) + " is a ListView: its children are the "
                                                 "rows it builds from listView.itemTemplateRef (set the prefab there).");
                    }
                }
                std::optional<std::string> name;
                if (reader.has("name"))
                {
                    name = reader.requireString("name");
                    if (name->empty())
                    {
                        throw ArgError("argument 'name' must not be empty");
                    }
                }
                if (reader.has("placement"))
                {
                    // Syntax first; the default size is the prefab's own, known once it is loaded.
                    readPlacement(reader.raw("placement"), glm::vec2(0.0f), "placement");
                }

                auto& dispatcher = events::EventDispatcher::instance();
                events::scene::LoadPrefabCommand load;
                load.filePath = file.string();
                if (parent.has_value())
                {
                    load.parent = parent->handle;
                }
                const std::optional<services::EntityHandle> loaded = dispatcher.execute(load);
                if (!loaded.has_value() || !loaded->isValid())
                {
                    return ToolResult::error("Loading '" + relative + "' failed: not a valid .vfPrefab (logs_read has the "
                                             "reason)");
                }
                const services::EntityHandle created = *loaded;

                std::optional<services::EntityData> canvas;
                try
                {
                    const std::optional<services::EntityData> data = findEntity(created);
                    if (!data.has_value())
                    {
                        throw std::runtime_error("the instantiated prefab vanished");
                    }
                    if (!isUIEntity(*data))
                    {
                        throw std::runtime_error("'" + relative + "' is not a UI prefab (its root has no UI component); "
                                                 "entity_instantiate_json or the content browser instantiate other prefabs");
                    }
                    const bool isCanvas = hasSection(created, Section::Canvas);
                    if (isCanvas)
                    {
                        if (hasUIAncestor(*data))
                        {
                            throw std::runtime_error("'" + relative + "' is a canvas, and a canvas inside UI would be "
                                                     "drawn twice: instantiate it without 'parent' or under a non-UI entity");
                        }
                        if (reader.has("placement"))
                        {
                            throw std::runtime_error("'" + relative + "' is a canvas: it always covers the whole screen, "
                                                     "so 'placement' does not apply");
                        }
                    }
                    canvas = findCanvas(*data);
                    if (!canvas.has_value())
                    {
                        throw std::runtime_error("'" + relative + "' is a UI fragment without a canvas: pass 'parent' (a "
                                                 "canvas, or a widget inside one)");
                    }

                    if (name.has_value())
                    {
                        events::scene::SetEntityNameCommand rename;
                        rename.entity = created;
                        rename.newName = *name;
                        dispatcher.execute(rename);
                    }

                    if (reader.has("placement"))
                    {
                        const std::optional<SectionData> rectSection = readSection(created, Section::Rect);
                        const std::optional<services::UICanvasData> canvasData = canvasDataOf(*data);
                        const std::optional<services::EntityData> holder =
                            data->parent.has_value() ? findEntity(*data->parent) : std::nullopt;
                        if (!rectSection.has_value() || !canvasData.has_value() || !holder.has_value())
                        {
                            throw std::runtime_error("'placement' needs a prefab root with a UIRect inside a canvas");
                        }
                        const auto& rect = std::get<services::UIRectData>(*rectSection);
                        const PlacementArgs placement =
                            readPlacement(reader.raw("placement"), referenceRectSize(rect, *canvasData), "placement");
                        const services::UIRectData placed =
                            placeRect(rect, placement, placementFrame(*holder, placement.relativeToCanvas));
                        writeSection(created, copyAuthored(*rectSection, SectionData{placed}));
                        const utilities::ui::AffineDelta delta = utilities::ui::centreDelta(
                            utilities::ui::affineRectOf(utilities::ui::toRectLayout(rect)),
                            utilities::ui::affineRectOf(utilities::ui::toRectLayout(placed)));
                        for (const RectMove& move : planDescendantMoves(*data, delta))
                        {
                            applyRectMove(move);
                        }
                    }

                    // Snapshot after everything, so one entry covers the instance as it now stands.
                    undo::UndoRecorder recorder("MCP: Instantiate UI prefab (" + relative + ")");
                    recorder.add(undo::EntityLifetimeUndo::capture(undo::EntityLifetimeUndo::Kind::Created, created));
                    recorder.push();
                }
                catch (...)
                {
                    deleteQuietly(created);  // not undoable: don't leave it behind
                    throw;
                }

                const std::optional<services::EntityData> instance = findEntity(created);
                const std::size_t listItems = instance.has_value() ? countListItems(*instance) : 0;
                nlohmann::json out{
                    {"entity", toId(created)},
                    {"name", instance.has_value() ? instance->name : name.value_or(std::string())},
                    {"path", relative},
                    {"parent", instance.has_value() ? entityId(instance->parent) : nlohmann::json(nullptr)},
                    {"canvasId", canvas.has_value() ? nlohmann::json(toId(canvas->handle)) : nlohmann::json(nullptr)},
                    {"listItemsBuilt", static_cast<int64_t>(listItems)},
                    {"widget", instance.has_value() ? widgetReport(*instance, {}) : nlohmann::json(nullptr)}
                };
                return reply(std::move(out), "Instantiated '" + relative + "' as entity " + std::to_string(toId(created)) +
                                                 (listItems > 0 ? " (" + std::to_string(listItems) + " list rows built)."
                                                                : "."));
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // ui_click
        // ------------------------------------------------------------------

        std::string clickRefusal(services::UIClickStatus status)
        {
            switch (status)
            {
            case services::UIClickStatus::Clicked:
                return {};
            case services::UIClickStatus::NotFound:
                return "the entity does not exist";
            case services::UIClickStatus::NotClickable:
                return "it is neither a button nor a checkbox (nor the label of a checkbox with labelToggle)";
            case services::UIClickStatus::NoRect:
                return "it has no UIRect, so the runtime never hit-tests it";
            case services::UIClickStatus::NotInteractable:
                return "it is not interactable (button / checkbox interactable is false; a script may have disabled it)";
            case services::UIClickStatus::Inactive:
                return "it or one of its ancestors is inactive (hidden)";
            case services::UIClickStatus::NoCanvas:
                return "it is not under a canvas, so the runtime never hit-tests it";
            case services::UIClickStatus::BlockedByModal:
                return "an open modal window blocks it (close the window first)";
            }
            return "the engine refused the click";
        }

        void registerClick(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "ui_click";
            tool.title = "Click UI widget";
            tool.description =
                "Play mode only: click a button or checkbox the way a real left click does, to check its script wiring. "
                "A button publishes Pressed, Released and Clicked (every script implementing IUIButtonListener gets "
                "onButtonPressed / onButtonReleased / onButtonClicked, synchronously); a checkbox toggles with the "
                "runtime rules (radio groups included) and publishes Toggled (IUICheckboxListener). The label child of a "
                "labelToggle checkbox clicks its checkbox. Same checks as a real click: interactable, active, under a "
                "canvas, not blocked by a modal window; no hover events. Scripts start on the first Play frame, so take "
                "a viewport_screenshot (or any call) after play_start first. Check the effect with logs_read, ui_get or "
                "viewport_screenshot. Not undoable (Play mode changes end with play_stop). Returns {entity, target, "
                "targetName, kind, published, toggles}.";
            tool.inputSchema = closedObject({
                {"entity", schema::entity("The button or checkbox (or a checkbox's label) to click")}
            }, {"entity"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"entity"}, "ui_click");
                auto& dispatcher = events::EventDispatcher::instance();
                if (!dispatcher.query(events::editor::IsPlayModeQuery{}))
                {
                    return ToolResult::error("ui_click works in Play mode only: call play_start first (in Edit mode no "
                                             "script runs and widgets do not react to clicks)");
                }
                bool loading = false;
                try
                {
                    loading = dispatcher.query(events::scene::IsSceneLoadPendingQuery{});
                }
                catch (const std::runtime_error&)
                {
                    // No scene persistence service: nothing can be loading.
                }
                if (loading)
                {
                    return ToolResult::error("Cannot click while a scene load is in progress; retry when it has finished.");
                }

                const ArgReader reader(args);
                const services::EntityData entity = requireEntity(reader, "entity");
                rejectPreviewSandbox(entity, "entity");
                events::ui::SimulateUIClickCommand click;
                click.entity = entity.handle;
                const services::UIClickResult result = dispatcher.execute(click);
                if (result.status != services::UIClickStatus::Clicked)
                {
                    return ToolResult::error("Cannot click " + describe(entity) + ": " + clickRefusal(result.status) + ".");
                }

                nlohmann::json published = nlohmann::json::array();
                nlohmann::json toggles = nlohmann::json::array();
                std::string kind;
                std::string summary;
                if (result.kind == services::UIClickKind::Checkbox)
                {
                    kind = "checkbox";
                    for (const services::UICheckboxToggleData& toggle : result.toggles)
                    {
                        published.push_back("UICheckboxToggled");
                        toggles.push_back({
                            {"entity", toId(toggle.entity)},
                            {"name", toggle.name},
                            {"checked", toggle.newChecked},
                            {"previous", toggle.previousChecked}
                        });
                    }
                    summary = result.toggles.empty()
                        ? "Clicked the checkbox '" + result.targetName + "': it is a checked radio button that cannot be "
                          "unchecked (allowUncheck is false), so nothing changed."
                        : "Clicked the checkbox '" + result.targetName + "': " + std::to_string(result.toggles.size()) +
                              " toggle(s) published to IUICheckboxListener scripts.";
                }
                else
                {
                    kind = "button";
                    published = {"UIButtonPressed", "UIButtonReleased", "UIButtonClicked"};
                    summary = "Clicked the button '" + result.targetName + "': IUIButtonListener scripts received "
                              "onButtonPressed, onButtonReleased and onButtonClicked.";
                }

                nlohmann::json out{
                    {"entity", toId(entity.handle)},
                    {"target", toId(result.target)},
                    {"targetName", result.targetName},
                    {"kind", kind},
                    {"published", std::move(published)},
                    {"toggles", std::move(toggles)},
                    {"note", "Listener scripts ran synchronously; how many listened cannot be counted. No hover events "
                             "were sent. Check the effect with logs_read, ui_get or viewport_screenshot."}
                };
                return reply(std::move(out), summary + " Check the effect with logs_read, ui_get or viewport_screenshot.");
            };
            registry.add(std::move(tool));
        }
    }

    void registerUIPrefabTools(ToolRegistry& registry, const ToolContext&)
    {
        registerSavePrefab(registry);
        registerInstantiatePrefab(registry);
        registerClick(registry);
    }
}
