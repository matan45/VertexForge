#include "CoreTools.hpp"
#include "EnvironmentToolSupport.hpp"
#include "SettingsTable.hpp"
#include "TerrainToolSupport.hpp"
#include "ToolHelpers.hpp"
#include "UIToolSupport.hpp"
#include "UIWidgets.hpp"
#include "../protocol/ArgReader.hpp"
#include "../undo/McpUndo.hpp"
#include "../undo/McpUndoCommands.hpp"

#include "events/EventDispatcher.hpp"
#include "events/scene/EntityTransformEvents.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// VK-1659 UI tools, part 1: ui_list_widget_types, ui_create_canvas, ui_add_widget (+ registerUITools, which also
// registers ui_get / ui_set (UIEditTools.cpp) and ui_save_prefab / ui_instantiate_prefab / ui_click
// (UIPrefabTools.cpp)). A widget is built from its recipe (UIWidgets.cpp) through the UI component events; the
// whole creation is ONE EntityLifetimeUndo (the prefab snapshot carries every UI component). Edits go through the
// section tables of UIToolSupport.
namespace mcp::tools
{
    namespace
    {
        using uitool::Section;
        using uitool::SectionData;

        constexpr const char* conventions =
            "Placement is in canvas REFERENCE pixels (the canvas' referenceResolution, 1920 x 1080 by default; they scale "
            "with the screen). Anchors are fractions of the WHOLE canvas, never of the parent, and a child never follows "
            "its parent, so prefer 'placement' (a preset such as topLeft / top / topRight / center / stretch with size "
            "[w, h] and margin) over raw rect values: it keeps a child glued inside its parent at every resolution. "
            "offset is [dx, dy] with +y DOWN. In raw rect values anchors and anchoredPosition are y-UP and pivot.y is "
            "measured from the TOP edge.";

        constexpr std::string_view canvasRenderMode = "screenSpaceOverlay";

        std::string typeList()
        {
            std::string out;
            for (std::string_view name : uitool::widgetTypeNames)
            {
                out += out.empty() ? "" : ", ";
                out += name;
            }
            return out;
        }

        uitool::WidgetType requireWidgetType(const ArgReader& reader)
        {
            const std::string name = reader.requireString("type");
            const std::optional<uitool::WidgetType> type = uitool::widgetTypeFromName(name);
            if (!type.has_value())
            {
                throw ArgError("argument 'type' must be one of " + typeList() + " (got '" + name + "')");
            }
            return *type;
        }

        nlohmann::json sectionDescriptions()
        {
            nlohmann::json out = nlohmann::json::object();
            for (std::size_t i = 0; i < uitool::sectionCount; ++i)
            {
                const auto section = static_cast<Section>(i);
                out[std::string(uitool::sectionKeys[i])] = {
                    {"component", std::string(uitool::sectionComponents[i])},
                    {"attachable", uitool::isAttachable(section)},
                    {"keys", uitool::sectionRanges(section)}
                };
            }
            return out;
        }

        nlohmann::json presetsJson()
        {
            nlohmann::json out = nlohmann::json::array();
            for (const utilities::ui::AnchorPreset& preset : utilities::ui::anchorPresets)
            {
                out.push_back(std::string(preset.name));
            }
            return out;
        }

        // ------------------------------------------------------------------
        // ui_list_widget_types
        // ------------------------------------------------------------------

        void registerListWidgetTypes(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "ui_list_widget_types";
            tool.title = "List UI widget types";
            tool.description =
                "The UI widget types ui_add_widget creates (" + typeList() + "): what each one is made of, its default "
                "size and starter style, what 'text' sets and its child parts; every section (one per UI component) "
                "with all its keys, kinds, ranges and enum values - the keys ui_set accepts and ui_get reports; the "
                "placement presets; the canvas facts. 'type' narrows it to one widget type and its sections. Works in "
                "any mode.";
            tool.inputSchema = closedObject({
                {"type", schema::enumString("Only this widget type (case-insensitive)",
                                            std::vector<std::string>(uitool::widgetTypeNames.begin(),
                                                                     uitool::widgetTypeNames.end()))}
            });
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"type"}, "ui_list_widget_types");
                const ArgReader reader(args);

                nlohmann::json widgets = nlohmann::json::array();
                nlohmann::json sections;
                if (reader.has("type"))
                {
                    const uitool::WidgetRecipe& recipe = uitool::recipeOf(requireWidgetType(reader));
                    widgets.push_back(uitool::recipeJson(recipe));
                    sections = nlohmann::json::object();
                    const nlohmann::json all = sectionDescriptions();
                    for (const Section section : recipe.sections)
                    {
                        const std::string key(uitool::sectionKey(section));
                        sections[key] = all.at(key);
                    }
                    for (std::size_t i = 0; i < uitool::sectionCount; ++i)
                    {
                        const std::string key(uitool::sectionKeys[i]);
                        if (uitool::isAttachable(static_cast<Section>(i)) && !sections.contains(key))
                        {
                            sections[key] = all.at(key);
                        }
                    }
                }
                else
                {
                    for (const uitool::WidgetRecipe& recipe : uitool::widgetRecipes())
                    {
                        widgets.push_back(uitool::recipeJson(recipe));
                    }
                    sections = sectionDescriptions();
                }

                nlohmann::json out{
                    {"widgets", std::move(widgets)},
                    {"sections", std::move(sections)},
                    {"placement", {
                        {"presets", presetsJson()},
                        {"keys", {
                            {"preset", "one of the presets (case-insensitive); default center"},
                            {"size", "[w, h] reference px (ignored on a stretched axis); default the widget's size"},
                            {"margin", "n | [x, y] | {left, right, top, bottom}: inward reference px from the edges the "
                                       "preset anchors to"},
                            {"offset", "[dx, dy] reference px added afterwards, +y DOWN"},
                            {"relativeTo", "parent (default: inside the parent widget, glued to it) | canvas"}
                        }},
                        {"note", conventions}
                    }},
                    {"canvas", {
                        {"renderMode", std::string(canvasRenderMode)},
                        {"note", "Play mode and the Runtime draw every canvas as a screen-space overlay (viewport_screenshot "
                                 "in Play shows it). In Edit mode a canvas is only a world-space preview quad at its entity "
                                 "(referenceResolution / pixelsPerUnit metres) showing textured images and labels. Images "
                                 "of all canvases draw before all labels; sortOrder orders canvases within each."}
                    }},
                    {"scripts", "Widgets are int entity ids: Entity::findByName(\"HealthBar\") (give HUD widgets unique "
                                "names), then UI::setLabelText / UI::setProgressBarValue / ...; a script implementing "
                                "IUIButtonListener gets every button's clicks (filter on the name). vf://docs/mtype-api "
                                "section 11 has a verified HUD script."}
                };
                return reply(std::move(out), "UI widget types, sections, placement presets and canvas facts.");
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // ui_create_canvas
        // ------------------------------------------------------------------

        void registerCreateCanvas(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "ui_create_canvas";
            tool.title = "Create UI canvas";
            tool.description =
                "Create a UI canvas: the root every HUD / menu widget lives under (ui_add_widget {parent: <canvas>}). "
                "renderMode is always screenSpaceOverlay: in Play mode and the Runtime the canvas covers the whole "
                "screen; in Edit mode it shows only as a world-space preview quad at the entity (referenceResolution / "
                "pixelsPerUnit metres, textured images and labels only), so check the result with play_start + "
                "viewport_screenshot. referenceResolution (default [1920, 1080]) is the pixel space every widget is "
                "laid out in; scaleMode scaleWithScreenSize (default) scales it uniformly by min(screen / reference), "
                "constantPixelSize keeps 1 reference px = 1 screen px. sortOrder orders canvases. A canvas cannot be "
                "placed inside other UI. Refused in Play mode, World mode and while a scene load is pending. One undo "
                "step. Returns {entity, name, parent, renderMode, canvas, rect, hint}.";
            nlohmann::json properties = transformProperties();
            properties["name"] = schema::string("Entity name. Default 'Canvas'.");
            properties["parent"] = schema::entity("Parent entity (not UI). Omit for the scene root.");
            nlohmann::json resolution = schema::array({{"type", "number"}},
                                                      "[width, height] reference pixels. Default [1920, 1080].");
            resolution["minItems"] = 2;
            resolution["maxItems"] = 2;
            properties["referenceResolution"] = std::move(resolution);
            properties["scaleMode"] = schema::enumString("Default scaleWithScreenSize.",
                                                         {"scaleWithScreenSize", "constantPixelSize"});
            properties["pixelsPerUnit"] = schema::numberRange("Reference px per metre of the Edit-mode preview quad. "
                                                              "Default 100.", 1.0, 1000.0);
            properties["sortOrder"] = schema::integerRange("Higher draws on top. Default 0.", -1000, 1000);
            properties["renderMode"] = schema::enumString("Only screenSpaceOverlay exists.",
                                                          {std::string(canvasRenderMode)});
            tool.inputSchema = closedObject(std::move(properties));
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"name", "parent", "referenceResolution", "scaleMode", "pixelsPerUnit",
                                              "sortOrder", "renderMode", "position", "rotationEuler", "scale"},
                                       "ui_create_canvas");
                if (const std::optional<std::string> blocked = env::editBlocker("create a UI canvas"))
                {
                    return ToolResult::error(*blocked);
                }
                if (isWorldModeActive())
                {
                    return ToolResult::error(uitool::worldModeRefusal);
                }

                const ArgReader reader(args);
                const std::string name = reader.optString("name", "Canvas");
                if (name.empty())
                {
                    throw ArgError("argument 'name' must not be empty");
                }
                if (reader.has("renderMode") &&
                    !env::detail::equalsIgnoreCase(reader.requireString("renderMode"), canvasRenderMode))
                {
                    throw ArgError("argument 'renderMode' must be screenSpaceOverlay (got '" +
                                   reader.requireString("renderMode") + "'): every canvas draws as a screen-space overlay "
                                   "in Play mode and the Runtime; there is no world-space or camera canvas. In Edit mode "
                                   "the canvas only shows as a preview quad at its entity.");
                }

                std::optional<services::EntityData> parent;
                if (reader.has("parent"))
                {
                    parent = requireEntity(reader, "parent");
                    rejectTerrain(*parent, "create a canvas under", "Put the canvas at the scene root.");
                    rejectOcean(*parent, "create a canvas under", "Put the canvas at the scene root.");
                    if (uitool::isUIEntity(*parent) || uitool::hasUIAncestor(*parent))
                    {
                        throw std::runtime_error("Cannot create a canvas under entity " + uitool::describe(*parent) +
                                                 ": it is UI, and a canvas inside UI would be drawn twice. Put the "
                                                 "canvas at the scene root (omit 'parent') or under a non-UI entity.");
                    }
                }

                // The canvas section, staged and validated before anything exists.
                nlohmann::json patch = nlohmann::json::object();
                if (reader.has("referenceResolution"))
                {
                    const nlohmann::json& resolution = reader.raw("referenceResolution");
                    if (!resolution.is_array() || resolution.size() != 2)
                    {
                        throw ArgError("argument 'referenceResolution' must be [width, height]");
                    }
                    patch["referenceWidth"] = resolution[0];
                    patch["referenceHeight"] = resolution[1];
                }
                for (const char* key : {"scaleMode", "pixelsPerUnit", "sortOrder"})
                {
                    if (reader.has(key))
                    {
                        patch[key] = reader.raw(key);
                    }
                }
                SectionData stagedCanvas = uitool::defaultSection(Section::Canvas);
                try
                {
                    stagedCanvas = uitool::patchSection(stagedCanvas, patch, "");
                }
                catch (const ArgError& e)
                {
                    if (reader.has("referenceResolution"))
                    {
                        throw ArgError(std::string(e.what()) + " (referenceResolution is [referenceWidth, referenceHeight])");
                    }
                    throw;
                }
                // Transform arguments are checked before anything is created.
                reader.optVec3("position");
                reader.optVec3("rotationEuler");
                reader.optVec3("scale");

                auto& dispatcher = events::EventDispatcher::instance();
                events::scene::CreateEntityCommand create;
                create.name = name;
                if (parent.has_value())
                {
                    create.parent = parent->handle;
                }
                const services::EntityHandle canvas = dispatcher.execute(create);
                if (!canvas.isValid())
                {
                    return ToolResult::error("CreateEntity failed");
                }

                try
                {
                    uitool::addSection(canvas, Section::Canvas);
                    if (!uitool::hasSection(canvas, Section::Rect))
                    {
                        uitool::addSection(canvas, Section::Rect);  // the canvas root covers the whole screen
                    }
                    const std::optional<SectionData> live = uitool::readSection(canvas, Section::Canvas);
                    if (!live.has_value())
                    {
                        throw std::runtime_error("the new canvas lost its UICanvas component");
                    }
                    uitool::writeSection(canvas, uitool::copyAuthored(*live, stagedCanvas));
                    if (hasTransformArgs(reader))
                    {
                        applyTransform(reader, canvas);
                    }

                    undo::UndoRecorder recorder("MCP: Create UI canvas (" + name + ")");
                    recorder.add(undo::EntityLifetimeUndo::capture(undo::EntityLifetimeUndo::Kind::Created, canvas));
                    recorder.push();
                }
                catch (...)
                {
                    uitool::deleteQuietly(canvas);  // not undoable: don't leave a half-built canvas behind
                    throw;
                }

                const std::optional<SectionData> canvasSection = uitool::readSection(canvas, Section::Canvas);
                const std::optional<SectionData> rectSection = uitool::readSection(canvas, Section::Rect);
                const services::UICanvasData& data = std::get<uitool::CanvasValues>(stagedCanvas).data;
                const std::string hint =
                    "Add widgets with ui_add_widget {parent: " + std::to_string(toId(canvas)) + ", type, name, placement: "
                    "{preset, size, margin}}. Check the HUD in Play: play_start, then viewport_screenshot {settleFrames: 5}.";

                nlohmann::json out{
                    {"entity", toId(canvas)},
                    {"name", name},
                    {"parent", parent.has_value() ? nlohmann::json(toId(parent->handle)) : nlohmann::json(nullptr)},
                    {"renderMode", std::string(canvasRenderMode)},
                    {"canvas", canvasSection.has_value() ? uitool::sectionJson(*canvasSection) : nlohmann::json(nullptr)},
                    {"rect", rectSection.has_value() ? uitool::sectionJson(*rectSection) : nlohmann::json(nullptr)},
                    {"hint", hint}
                };
                return reply(std::move(out), "Created the UI canvas '" + name + "' (entity " +
                             std::to_string(toId(canvas)) + "), " + std::to_string(static_cast<int>(data.referenceWidth)) +
                             " x " + std::to_string(static_cast<int>(data.referenceHeight)) + " reference px. " + hint);
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // ui_add_widget
        // ------------------------------------------------------------------

        nlohmann::json addWidgetSchema()
        {
            nlohmann::json properties{
                {"type", schema::enumString("Widget type (case-insensitive); ui_list_widget_types describes each",
                                            std::vector<std::string>(uitool::widgetTypeNames.begin(),
                                                                     uitool::widgetTypeNames.end()))},
                {"parent", schema::entity("A canvas (ui_create_canvas) or a widget inside one")},
                {"name", schema::string("Entity name; scripts find widgets by it (Entity::findByName), so keep HUD names "
                                        "unique. Default: the type name.")},
                {"text", schema::string("The caption: a Label's text, a Button's label, a Checkbox's label part, a "
                                        "Dropdown's / TextInput's placeholder")},
                {"placement", uitool::placementSchema(
                    "Where the widget goes, in reference px: {preset, size, margin, offset, relativeTo}. Default: "
                    "centred in the parent at the widget's default size.")},
                {"active", schema::boolean("false creates it hidden. Default true.")}
            };
            for (std::size_t i = 0; i < uitool::sectionCount; ++i)
            {
                const auto section = static_cast<Section>(i);
                if (section == Section::Canvas)
                {
                    continue;
                }
                const std::string key(uitool::sectionKeys[i]);
                const std::string description = section == Section::Rect
                    ? std::string("Raw rect values instead of 'placement' (they override the default centred placement): "
                                  "anchors are canvas fractions (y up), pivot.y is from the TOP")
                    : "Initial " + key + " values (" + std::string(uitool::sectionComponents[i]) + ")" +
                          (uitool::isAttachable(section) ? "; attached when the type has none" : "");
                properties[key] = uitool::sectionSchema(section, description);
            }
            return closedObject(std::move(properties), {"type", "parent"});
        }

        void registerAddWidget(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "ui_add_widget";
            tool.title = "Add UI widget";
            tool.description =
                "Create a UI widget under a canvas or a widget inside one: type " + typeList() + " (ui_list_widget_types "
                "lists what each is made of, its starter style and every section key). " + std::string(conventions) +
                " 'text' sets the caption; section objects (label, button, progressBar, ...) set initial values and are "
                "strict (unknown keys or out-of-range values create nothing). A Button carries its own image and label "
                "(colour it with button.normalColor); a Checkbox gets a '<name>Label' child part. Warns about duplicate "
                "names (scripts' Entity::findByName takes the first). Refused in Play mode, World mode and while a scene "
                "load is pending. One undo step. Returns {entity, type, name, parent, canvasId, parts, resolved (x, y, w, "
                "h in reference px, y down), widget (as ui_get reports it), warnings}.";
            tool.inputSchema = addWidgetSchema();
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"type", "parent", "name", "text", "placement", "active", "rect", "image",
                                              "label", "button", "checkbox", "slider", "progressBar", "dropdown",
                                              "textInput", "layoutGroup", "listView", "mask", "scroll"},
                                       "ui_add_widget");
                if (const std::optional<std::string> blocked = env::editBlocker("add a UI widget"))
                {
                    return ToolResult::error(*blocked);
                }
                if (isWorldModeActive())
                {
                    return ToolResult::error(uitool::worldModeRefusal);
                }

                const ArgReader reader(args);
                const uitool::WidgetType type = requireWidgetType(reader);
                const uitool::WidgetRecipe& recipe = uitool::recipeOf(type);
                const std::string typeName(uitool::widgetTypeNames[static_cast<std::size_t>(type)]);

                const services::EntityData parent = requireEntity(reader, "parent");
                uitool::rejectPreviewSandbox(parent, "parent");
                const std::optional<services::EntityData> canvas = uitool::findCanvas(parent);
                if (!canvas.has_value())
                {
                    throw std::runtime_error("Entity " + uitool::describe(parent) + " (argument 'parent') is neither a "
                                             "canvas nor inside one: create one with ui_create_canvas and add widgets "
                                             "under it.");
                }
                if (uitool::isListItem(parent.handle))
                {
                    throw std::runtime_error("Entity " + uitool::describe(parent) + " is a ListView row: rows are rebuilt "
                                             "from the list's item template (never saved), so put the widget in the "
                                             "template prefab instead.");
                }
                if (uitool::hasSection(parent.handle, Section::ListView))
                {
                    throw std::runtime_error("Entity " + uitool::describe(parent) + " is a ListView: its children are its "
                                             "rows, built from listView.itemTemplateRef. Build a row with ui_add_widget "
                                             "elsewhere, ui_save_prefab it and set it as the template.");
                }

                const std::string name = reader.optString("name", typeName);
                if (name.empty())
                {
                    throw ArgError("argument 'name' must not be empty");
                }
                const bool hasPlacement = reader.has("placement");
                const bool hasRect = reader.has("rect");
                if (hasPlacement && hasRect)
                {
                    throw ArgError("pass 'placement' (a preset with size and margin) or 'rect' (raw values), not both");
                }
                const std::optional<bool> active = reader.has("active") ? std::optional<bool>(reader.requireBool("active"))
                                                                       : std::nullopt;

                // ---- Stage every section before anything exists ----
                std::map<Section, SectionData> staged = uitool::starterSections(type);
                std::optional<std::string> text;
                if (reader.has("text"))
                {
                    text = reader.requireString("text");
                    uitool::applyText(type, staged, *text);
                }

                std::vector<Section> attached;  // attachable sections the type does not have
                for (std::size_t i = 0; i < uitool::sectionCount; ++i)
                {
                    const auto section = static_cast<Section>(i);
                    const std::string key(uitool::sectionKeys[i]);
                    if (section == Section::Rect || section == Section::Canvas || !reader.has(key.c_str()))
                    {
                        continue;
                    }
                    auto it = staged.find(section);
                    if (it == staged.end())
                    {
                        if (section == Section::Label && type == uitool::WidgetType::Checkbox)
                        {
                            throw ArgError("a Checkbox's text is on its label part (a child entity): pass 'text', or "
                                           "ui_set the part's label section after creating it");
                        }
                        if (!uitool::isAttachable(section))
                        {
                            throw ArgError("a " + typeName + " widget has no '" + key + "' section (its sections: " +
                                           uitool::sectionList(recipe.sections) + ")");
                        }
                        it = staged.emplace(section, uitool::defaultSection(section)).first;
                        attached.push_back(section);
                    }
                    it->second = uitool::patchSection(it->second, reader.raw(key.c_str()), key);
                }

                // A picture shows its own colours unless the call tints it.
                if (uitool::patchSets(args, "image", "textureRef") && !uitool::patchSets(args, "image", "colorTint"))
                {
                    auto& image = std::get<services::UIImageData>(staged.at(Section::Image));
                    if (image.textureRef.isValid())
                    {
                        image.colorTint = glm::vec4(1.0f);
                    }
                }
                // The dropdown's own label shows the selection or the placeholder.
                if (staged.contains(Section::Dropdown) && staged.contains(Section::Label) &&
                    !uitool::patchSets(args, "label", "text"))
                {
                    std::get<services::UILabelData>(staged.at(Section::Label)).text =
                        uitool::dropdownCaption(std::get<services::UIDropdownData>(staged.at(Section::Dropdown)));
                }

                // ---- The rect ----
                const std::optional<services::UICanvasData> canvasData = uitool::canvasDataOf(parent);
                if (!canvasData.has_value())
                {
                    throw std::runtime_error("The canvas of entity " + uitool::describe(parent) + " cannot be read");
                }
                uitool::PlacementArgs placement;
                placement.placement.size = recipe.defaultSize;
                if (hasPlacement)
                {
                    placement = uitool::readPlacement(reader.raw("placement"), recipe.defaultSize, "placement");
                }
                const utilities::ui::AffineRect frame = uitool::placementFrame(parent, placement.relativeToCanvas);
                services::UIRectData rect =
                    uitool::placeRect(std::get<services::UIRectData>(staged.at(Section::Rect)), placement, frame);
                if (hasRect)
                {
                    rect = std::get<services::UIRectData>(uitool::patchSection(SectionData{rect}, reader.raw("rect"), "rect"));
                }
                staged[Section::Rect] = rect;

                // ---- Warnings, decided before the entity exists ----
                std::vector<std::string> warnings;
                if (const std::size_t sameName = uitool::countEntitiesNamed(name); sameName > 0)
                {
                    warnings.push_back(std::to_string(sameName) + " other entit" + (sameName == 1 ? "y is" : "ies are") +
                                       " already named '" + name + "': a script's Entity::findByName(\"" + name +
                                       "\") returns the first one. Give HUD widgets unique names.");
                }
                if (uitool::hasSection(parent.handle, Section::LayoutGroup))
                {
                    warnings.push_back("The parent's layout group positions this widget every frame (in Play), so its "
                                       "placement only sets its size there.");
                }
                const utilities::ui::PixelRect atReference = utilities::ui::resolveRect(
                    utilities::ui::toRectLayout(rect), canvasData->referenceWidth, canvasData->referenceHeight, 1.0f);
                if (atReference.x + atReference.w <= 0.0f || atReference.y + atReference.h <= 0.0f ||
                    atReference.x >= canvasData->referenceWidth || atReference.y >= canvasData->referenceHeight)
                {
                    warnings.push_back("At the reference resolution the widget lies outside the canvas.");
                }
                const std::optional<SectionData> canvasSection = uitool::readSection(canvas->handle, Section::Canvas);
                if (canvasSection.has_value() && !std::get<uitool::CanvasValues>(*canvasSection).theme.empty())
                {
                    warnings.push_back("The canvas has a theme ('" + std::get<uitool::CanvasValues>(*canvasSection).theme +
                                       "'): it restyles widgets with a style key on every scene load.");
                }

                // ---- Create ----
                auto& dispatcher = events::EventDispatcher::instance();
                events::scene::CreateEntityCommand create;
                create.name = name;
                create.parent = parent.handle;
                const services::EntityHandle root = dispatcher.execute(create);
                if (!root.isValid())
                {
                    return ToolResult::error("CreateEntity failed");
                }

                std::optional<services::EntityHandle> labelPart;
                try
                {
                    // The primary component first: the engine auto-adds its siblings (Button -> Image + Label).
                    uitool::addSection(root, recipe.primary);
                    for (const Section section : recipe.sections)
                    {
                        if (!uitool::hasSection(root, section))
                        {
                            uitool::addSection(root, section);
                        }
                    }
                    for (const Section section : attached)
                    {
                        if (!uitool::hasSection(root, section))
                        {
                            uitool::addSection(root, section);
                        }
                    }
                    for (const auto& [section, data] : staged)
                    {
                        const std::optional<SectionData> live = uitool::readSection(root, section);
                        if (!live.has_value())
                        {
                            throw std::runtime_error("the new widget lacks its " + std::string(uitool::sectionKey(section)) +
                                                     " section");
                        }
                        uitool::writeSection(root, uitool::copyAuthored(*live, data));
                    }

                    if (type == uitool::WidgetType::Checkbox)
                    {
                        events::scene::CreateEntityCommand createPart;
                        createPart.name = name + std::string(uitool::checkboxPartSuffix);
                        createPart.parent = root;
                        const services::EntityHandle part = dispatcher.execute(createPart);
                        if (!part.isValid())
                        {
                            throw std::runtime_error("creating the checkbox's label part failed");
                        }
                        labelPart = part;
                        uitool::addSection(part, Section::Label);
                        if (!uitool::hasSection(part, Section::Rect))
                        {
                            uitool::addSection(part, Section::Rect);
                        }
                        const std::optional<SectionData> partRect = uitool::readSection(part, Section::Rect);
                        const std::optional<SectionData> partLabel = uitool::readSection(part, Section::Label);
                        if (!partRect.has_value() || !partLabel.has_value())
                        {
                            throw std::runtime_error("the checkbox's label part lacks its rect or label");
                        }
                        uitool::writeSection(part, uitool::copyAuthored(*partRect, SectionData{uitool::checkboxPartRect(rect)}));
                        uitool::writeSection(part, uitool::copyAuthored(
                                                       *partLabel, SectionData{uitool::checkboxPartLabel(
                                                                       text.value_or(uitool::defaultText(type, name)))}));
                    }

                    if (active.has_value() && !*active)
                    {
                        events::scene::SetEntityActiveCommand hide;
                        hide.entity = root;
                        hide.isActive = false;
                        dispatcher.execute(hide);
                    }

                    // Snapshot after everything, so one entry covers the whole widget.
                    undo::UndoRecorder recorder("MCP: Add UI " + typeName + " (" + name + ")");
                    recorder.add(undo::EntityLifetimeUndo::capture(undo::EntityLifetimeUndo::Kind::Created, root));
                    recorder.push();
                }
                catch (...)
                {
                    uitool::deleteQuietly(root);  // not undoable: don't leave a half-built widget behind
                    throw;
                }

                const std::optional<services::EntityData> created = findEntity(root);
                nlohmann::json parts = nlohmann::json::object();
                if (labelPart.has_value())
                {
                    parts["label"] = toId(*labelPart);
                }
                nlohmann::json out{
                    {"entity", toId(root)},
                    {"type", typeName},
                    {"name", name},
                    {"parent", toId(parent.handle)},
                    {"canvasId", toId(canvas->handle)},
                    {"parts", std::move(parts)},
                    {"resolved", uitool::resolvedJson(rect, *canvasData, std::nullopt)},
                    {"widget", created.has_value() ? uitool::widgetReport(*created, {}) : nlohmann::json(nullptr)},
                    {"warnings", warnings}
                };

                std::string summary = "Added the " + typeName + " '" + name + "' (entity " + std::to_string(toId(root)) +
                                      ") to " + uitool::describe(parent) + " at (" +
                                      std::to_string(static_cast<int>(atReference.x)) + ", " +
                                      std::to_string(static_cast<int>(atReference.y)) + "), " +
                                      std::to_string(static_cast<int>(atReference.w)) + " x " +
                                      std::to_string(static_cast<int>(atReference.h)) + " reference px.";
                for (const std::string& warning : warnings)
                {
                    summary += " Warning: " + warning;
                }
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }
    }

    void registerUITools(ToolRegistry& registry, const ToolContext& context)
    {
        registerListWidgetTypes(registry);
        registerCreateCanvas(registry);
        registerAddWidget(registry);
        uitool::registerUIEditTools(registry, context);
        uitool::registerUIPrefabTools(registry, context);
    }
}
