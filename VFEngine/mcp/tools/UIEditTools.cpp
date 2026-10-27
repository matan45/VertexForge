#include "CoreTools.hpp"
#include "EnvironmentToolSupport.hpp"
#include "SettingsTable.hpp"
#include "ToolHelpers.hpp"
#include "UIToolSupport.hpp"
#include "UIWidgets.hpp"
#include "../protocol/ArgReader.hpp"
#include "../undo/McpUndo.hpp"
#include "../undo/McpUndoCommands.hpp"
#include "../undo/UIUndo.hpp"

#include "events/EventDispatcher.hpp"
#include "events/scene/EntityTransformEvents.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// VK-1659 UI tools, part 2: ui_get (the report of one widget, a subtree, or every canvas) and ui_set (strict,
// all-or-nothing section edits, rename, show / hide, placement; one undo step per call). Everything ui_get reports
// passes back to ui_set unchanged (changed:false).
namespace mcp::tools::uitool
{
    namespace
    {
        constexpr std::size_t maxRecursiveWidgets = 500;
        constexpr double maxViewportExtent = 16384.0;

        bool contains(const std::vector<Section>& sections, Section section)
        {
            return std::find(sections.begin(), sections.end(), section) != sections.end();
        }

        std::optional<glm::vec2> readViewport(const ArgReader& reader)
        {
            if (!reader.has("viewport"))
            {
                return std::nullopt;
            }
            const nlohmann::json& value = reader.raw("viewport");
            if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number())
            {
                throw ArgError("argument 'viewport' must be [width, height] in pixels");
            }
            const double w = value[0].get<double>();
            const double h = value[1].get<double>();
            if (!(w >= 1.0 && w <= maxViewportExtent && h >= 1.0 && h <= maxViewportExtent))
            {
                throw ArgError("argument 'viewport' must be [width, height] with each between 1 and 16384 pixels");
            }
            return glm::vec2(static_cast<float>(w), static_cast<float>(h));
        }

        // Why a report key cannot be set, and what sets it instead.
        std::string readOnlyHint(std::string_view key)
        {
            if (key == "parent" || key == "children")
            {
                return "move entities with entity_set_parent";
            }
            if (key == "type" || key == "components")
            {
                return "the components decide it; attach image / label / layoutGroup / mask / scroll sections with ui_set";
            }
            if (key == "effectivelyActive")
            {
                return "it follows 'active' of the entity and its ancestors";
            }
            if (key == "styleKey")
            {
                return "styles and themes are not edited through MCP";
            }
            if (key == "parts")
            {
                return "parts are the widget's child entities: edit them with ui_set on their own ids";
            }
            return "it describes the widget";
        }

        bool sameOptions(const services::UIDropdownData& a, const services::UIDropdownData& b)
        {
            if (a.options.size() != b.options.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < a.options.size(); ++i)
            {
                if (a.options[i].text != b.options[i].text)
                {
                    return false;
                }
            }
            return true;
        }

        // ------------------------------------------------------------------
        // ui_get
        // ------------------------------------------------------------------

        nlohmann::json canvasSummary(const services::EntityData& canvas)
        {
            const std::optional<SectionData> section = readSection(canvas.handle, Section::Canvas);
            bool truncated = false;
            const std::vector<services::EntityData> widgets = uiDescendants(canvas, maxRecursiveWidgets, &truncated);
            return {
                {"entity", toId(canvas.handle)},
                {"name", canvas.name},
                {"active", canvas.isActive},
                {"effectivelyActive", canvas.isEffectivelyActive},
                {"parent", entityId(canvas.parent)},
                {"canvas", section.has_value() ? sectionJson(*section) : nlohmann::json(nullptr)},
                {"children", static_cast<int64_t>(canvas.children.size())},
                {"widgets", static_cast<int64_t>(widgets.size())},
                {"widgetsTruncated", truncated}
            };
        }

        void registerGet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "ui_get";
            tool.title = "Get UI";
            tool.description =
                "Read UI. Without 'entity': every canvas of the scene {entity, name, active, canvas, children, widgets} "
                "(the layer open in the UI Layer Builder is the builder's working copy: not listed, and refused). With "
                "'entity' (a canvas or a widget): its report {entity, name, type, active, effectivelyActive, parent, "
                "canvasId, children, parts, layoutControlled, listItem, components, one key per section (canvas, rect, "
                "image, label, button, ...), styleKey, resolved {x, y, w, h, viewport, scale} (reference px, top-left "
                "origin, y down; at 'viewport' [w, h] when given), validation (only when a compound widget is malformed)}. "
                "The report passes back to ui_set unchanged. recursive:true reports the widget and every UI entity below "
                "it in hierarchy order ({widgets: [...]}, at most 500). ranges:true adds every section's keys and ranges. "
                "Works in any mode: in Play it shows live values (e.g. a progress bar a script moves).";
            nlohmann::json viewport = schema::array({{"type", "number"}},
                                                    "[width, height] pixels to resolve the rects at (default: the canvas "
                                                    "reference resolution)");
            viewport["minItems"] = 2;
            viewport["maxItems"] = 2;
            tool.inputSchema = closedObject({
                {"entity", schema::entity("A canvas or widget. Omit to list the canvases.")},
                {"recursive", schema::boolean("Also report every UI entity below it. Default false.")},
                {"ranges", schema::boolean("Add {section: {key: range}} for the reported sections. Default false.")},
                {"viewport", std::move(viewport)}
            });
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"entity", "recursive", "ranges", "viewport"}, "ui_get");
                const ArgReader reader(args);
                ReportOptions options;
                options.ranges = reader.optBool("ranges", false);
                options.viewport = readViewport(reader);
                const bool recursive = reader.optBool("recursive", false);

                if (!reader.has("entity"))
                {
                    events::scene::GetEntitiesWithComponentQuery query;
                    query.componentType = services::ComponentTypeId::UICanvas;
                    const std::vector<services::EntityHandle> handles = events::EventDispatcher::instance().query(query);
                    nlohmann::json canvases = nlohmann::json::array();
                    for (const services::EntityHandle& handle : handles)
                    {
                        if (inPreviewSandbox(handle))
                        {
                            continue; // an open UI Layer Builder's working copy: not part of the scene
                        }
                        if (const std::optional<services::EntityData> canvas = findEntity(handle))
                        {
                            canvases.push_back(canvasSummary(*canvas));
                        }
                    }
                    const std::size_t count = canvases.size();
                    nlohmann::json out{{"canvases", std::move(canvases)}};
                    if (count == 0)
                    {
                        out["hint"] = "No canvas yet: ui_create_canvas makes one, then ui_add_widget adds widgets.";
                    }
                    return reply(std::move(out), count == 0 ? std::string("No UI canvas in the scene.")
                                                            : std::to_string(count) + " UI canvas(es).");
                }

                const services::EntityData entity = requireUIEntity(args, "entity");
                if (!recursive)
                {
                    nlohmann::json report = widgetReport(entity, options);
                    const std::string type = report.value("type", std::string());
                    return reply(std::move(report), "UI " + (type.empty() ? std::string("entity") : type) + " " +
                                                        describe(entity) + ".");
                }

                nlohmann::json widgets = nlohmann::json::array();
                widgets.push_back(widgetReport(entity, options));
                bool truncated = false;
                for (const services::EntityData& descendant : uiDescendants(entity, maxRecursiveWidgets - 1, &truncated))
                {
                    if (isUIEntity(descendant))
                    {
                        widgets.push_back(widgetReport(descendant, options));
                    }
                }
                const std::size_t count = widgets.size();
                nlohmann::json out{{"widgets", std::move(widgets)}, {"count", count}, {"truncated", truncated}};
                return reply(std::move(out), std::to_string(count) + " UI entities under " + describe(entity) +
                                                 (truncated ? " (truncated)." : "."));
            };
            registry.add(std::move(tool));
        }

        // ------------------------------------------------------------------
        // ui_set
        // ------------------------------------------------------------------

        struct StagedSection
        {
            Section section = Section::Rect;
            std::optional<SectionData> before;  // nullopt: attach
            SectionData after;
        };

        StagedSection* findStaged(std::vector<StagedSection>& staged, Section section)
        {
            for (StagedSection& entry : staged)
            {
                if (entry.section == section)
                {
                    return &entry;
                }
            }
            return nullptr;
        }

        nlohmann::json setSchema()
        {
            nlohmann::json properties{
                {"entity", schema::entity("The canvas or widget to change")},
                {"name", schema::string("New entity name (scripts find widgets by name)")},
                {"active", schema::boolean("Show (true) or hide (false) it and everything below it")},
                {"placement", placementSchema("Re-place it: {preset, size (default: its current size), margin, offset, "
                                              "relativeTo} in reference px")},
                {"moveDescendants", schema::boolean("When the rect moves, move every UI entity below it by the same "
                                                    "amount (positions only, sizes are kept). Default true.")}
            };
            for (std::size_t i = 0; i < sectionCount; ++i)
            {
                const auto section = static_cast<Section>(i);
                const std::string key(sectionKeys[i]);
                std::string description = "Strict partial update of the " + std::string(sectionComponents[i]) +
                                          " values (unknown keys or out-of-range values change nothing)";
                if (isAttachable(section))
                {
                    description += "; attached when the entity has none";
                }
                if (section == Section::Rect)
                {
                    description += ". Anchors are canvas fractions (y up), pivot.y is from the TOP; prefer 'placement'";
                }
                properties[key] = sectionSchema(section, description);
            }
            for (std::string_view key : reportIdentityKeys)
            {
                properties[std::string(key)] = {{"description", "Read-only (from ui_get): accepted when unchanged"}};
            }
            for (std::string_view key : reportDerivedKeys)
            {
                properties[std::string(key)] = {{"description", "Read-only (from ui_get): ignored"}};
            }
            return closedObject(std::move(properties), {"entity"});
        }

        void registerSet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "ui_set";
            tool.title = "Set UI";
            tool.description =
                "Change a canvas or widget: section objects (canvas, rect, image, label, button, checkbox, slider, "
                "progressBar, dropdown, textInput, layoutGroup, listView, mask, scroll) are strict partial updates - "
                "unknown keys, wrong types or out-of-range values fail the whole call and change nothing; "
                "ui_list_widget_types lists every key and range. image, label, layoutGroup, mask and scroll are attached "
                "when the entity has none. Also name, active (show / hide) and placement {preset, size, margin, offset, "
                "relativeTo} (reference px; moveDescendants, default true, moves every UI entity below it along). A "
                "whole ui_get report passes back unchanged (changed:false); its read-only keys must keep their values. "
                "Colour a button with button.normalColor (the state colour overwrites image.colorTint every frame); a "
                "checkbox's text is on its label part (another entity). Changing dropdown options or selectedIndex also "
                "updates its label. Refused for ListView rows (engine-managed), for a canvas' rect, in Play mode and "
                "while a scene load is pending. One undo step, none when nothing changes. Returns {entity, changed, "
                "changes, widget (the new report), movedDescendants, warnings}.";
            tool.inputSchema = setSchema();
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"entity", "name", "active", "placement", "moveDescendants",
                                              "canvas", "rect", "image", "label", "button", "checkbox", "slider",
                                              "progressBar", "dropdown", "textInput", "layoutGroup", "listView", "mask",
                                              "scroll",
                                              "type", "effectivelyActive", "parent", "canvasId", "children", "parts",
                                              "layoutControlled", "listItem", "components", "styleKey",
                                              "resolved", "validation", "ranges"},
                                       "ui_set");
                if (const std::optional<std::string> blocked = env::editBlocker("edit UI"))
                {
                    return ToolResult::error(*blocked);
                }

                const ArgReader reader(args);
                const services::EntityData entity = requireUIEntity(args, "entity");
                if (isListItem(entity.handle))
                {
                    throw std::runtime_error("Entity " + describe(entity) + " is a ListView row: rows are rebuilt from the "
                                             "list's item template and never saved. Edit the template prefab, or the "
                                             "ListView itself.");
                }

                // Read-only report keys pass only when they echo the current report.
                const nlohmann::json current = widgetReport(entity, {});
                for (std::string_view keyView : reportIdentityKeys)
                {
                    const std::string key(keyView);
                    if (!reader.has(key.c_str()))
                    {
                        continue;
                    }
                    const auto it = current.find(key);
                    if (it == current.end() || *it != args.at(key))
                    {
                        throw ArgError("'" + key + "' is read-only here: " + readOnlyHint(key) + " (its current value is " +
                                       (it == current.end() ? std::string("absent") : it->dump()) + ")");
                    }
                }

                const bool hasPlacement = reader.has("placement");
                if (hasPlacement && reader.has("rect"))
                {
                    throw ArgError("pass 'placement' (a preset with size and margin) or 'rect' (raw values), not both");
                }
                const bool isCanvasRoot = hasSection(entity.handle, Section::Canvas);
                if (isCanvasRoot && hasPlacement)
                {
                    throw std::runtime_error("Entity " + describe(entity) + " is a canvas: it always covers the whole "
                                             "screen, so it cannot be placed. Place its widgets instead.");
                }
                const bool moveDescendants = reader.optBool("moveDescendants", true);

                std::optional<std::string> newName;
                if (reader.has("name"))
                {
                    newName = reader.requireString("name");
                    if (newName->empty())
                    {
                        throw ArgError("argument 'name' must not be empty");
                    }
                }
                const std::optional<bool> active =
                    reader.has("active") ? std::optional<bool>(reader.requireBool("active")) : std::nullopt;

                // ---- Stage every section before anything is written ----
                const std::vector<Section> present = presentSections(entity.handle);
                // A Button, Checkbox, TextInput or Dropdown repaints its image every frame (Edit mode too) from its
                // state colours, so its image.colorTint is the engine's, not authored: it is checked like any value but
                // never written, and an echoed (possibly mid-transition) tint never counts as a change.
                std::optional<Section> tintOwner;
                for (const Section owner : {Section::Button, Section::Checkbox, Section::TextInput, Section::Dropdown})
                {
                    if (contains(present, owner))
                    {
                        tintOwner = owner;
                        break;
                    }
                }
                std::vector<std::string> warnings;
                std::vector<StagedSection> staged;
                for (std::size_t i = 0; i < sectionCount; ++i)
                {
                    const auto section = static_cast<Section>(i);
                    const std::string key(sectionKeys[i]);
                    if (!reader.has(key.c_str()))
                    {
                        continue;
                    }
                    std::optional<SectionData> before = readSection(entity.handle, section);
                    if (!before.has_value())
                    {
                        if (section == Section::Label && contains(present, Section::Checkbox))
                        {
                            const std::optional<services::EntityHandle> part = checkboxLabelPart(entity);
                            throw std::runtime_error(
                                "Entity " + describe(entity) + " is a checkbox box; its text is on its label part" +
                                (part.has_value() ? ", entity " + std::to_string(toId(*part)) + ": ui_set {entity: " +
                                                        std::to_string(toId(*part)) + ", label: {...}}"
                                                  : " (it has none; add one with ui_add_widget {type: \"Label\"} under it)") +
                                ".");
                        }
                        if (!isAttachable(section))
                        {
                            throw std::runtime_error("Entity " + describe(entity) + " has no " +
                                                     std::string(sectionComponents[i]) + " (its sections: " +
                                                     sectionList(present) + "); only image, label, layoutGroup, mask "
                                                     "and scroll can be attached with ui_set");
                        }
                        staged.push_back({section, std::nullopt,
                                          patchSection(defaultSection(section), reader.raw(key.c_str()), key)});
                        continue;
                    }
                    SectionData after = patchSection(*before, reader.raw(key.c_str()), key);
                    if (section == Section::Image && tintOwner.has_value() && patchSets(args, "image", "colorTint"))
                    {
                        auto& afterImage = std::get<services::UIImageData>(after);
                        const auto& beforeImage = std::get<services::UIImageData>(*before);
                        const glm::vec4 difference = glm::abs(afterImage.colorTint - beforeImage.colorTint);
                        if (std::max({difference.r, difference.g, difference.b, difference.a}) > 1e-3f)
                        {
                            const std::string owner(sectionKey(*tintOwner));
                            warnings.push_back("image.colorTint was not changed: a " + owner + " repaints its image "
                                               "every frame from its state colours; set " + owner + ".normalColor (and "
                                               "hoveredColor, pressedColor, ...) instead.");
                        }
                        afterImage.colorTint = beforeImage.colorTint;
                    }
                    staged.push_back({section, std::move(before), std::move(after)});
                }

                if (isCanvasRoot)
                {
                    if (const StagedSection* rect = findStaged(staged, Section::Rect);
                        rect != nullptr && rect->before.has_value() && !sameAuthored(*rect->before, rect->after))
                    {
                        throw std::runtime_error("Entity " + describe(entity) + " is a canvas: its rect always covers the "
                                                 "whole screen and cannot be edited. Change its widgets instead.");
                    }
                }

                if (hasPlacement)
                {
                    const std::optional<SectionData> rectSection = readSection(entity.handle, Section::Rect);
                    if (!rectSection.has_value())
                    {
                        throw std::runtime_error("Entity " + describe(entity) + " has no UIRect to place");
                    }
                    const std::optional<services::UICanvasData> canvasData = canvasDataOf(entity);
                    if (!canvasData.has_value() || !entity.parent.has_value())
                    {
                        throw std::runtime_error("Entity " + describe(entity) + " is not inside a canvas, so it cannot be "
                                                 "placed");
                    }
                    const std::optional<services::EntityData> parent = findEntity(*entity.parent);
                    if (!parent.has_value())
                    {
                        throw std::runtime_error("The parent of entity " + describe(entity) + " does not exist");
                    }
                    const auto& rect = std::get<services::UIRectData>(*rectSection);
                    const PlacementArgs placement =
                        readPlacement(reader.raw("placement"), referenceRectSize(rect, *canvasData), "placement");
                    const utilities::ui::AffineRect frame = placementFrame(*parent, placement.relativeToCanvas);
                    staged.push_back({Section::Rect, rectSection, SectionData{placeRect(rect, placement, frame)}});
                }

                // The dropdown's own label follows its options / selection unless the call sets the label text.
                if (const StagedSection* dropdown = findStaged(staged, Section::Dropdown);
                    dropdown != nullptr && dropdown->before.has_value() && !patchSets(args, "label", "text"))
                {
                    const auto& before = std::get<services::UIDropdownData>(*dropdown->before);
                    const auto& after = std::get<services::UIDropdownData>(dropdown->after);
                    if (!sameOptions(before, after) || before.selectedIndex != after.selectedIndex ||
                        before.placeholderText != after.placeholderText)
                    {
                        const std::string caption = dropdownCaption(after);
                        if (StagedSection* label = findStaged(staged, Section::Label))
                        {
                            std::get<services::UILabelData>(label->after).text = caption;
                        }
                        else if (std::optional<SectionData> labelBefore = readSection(entity.handle, Section::Label))
                        {
                            services::UILabelData labelAfter = std::get<services::UILabelData>(*labelBefore);
                            labelAfter.text = caption;
                            staged.push_back({Section::Label, std::move(labelBefore), SectionData{std::move(labelAfter)}});
                        }
                    }
                }

                // ---- What actually changes ----
                std::vector<StagedSection> changes;
                for (StagedSection& entry : staged)
                {
                    if (!entry.before.has_value() || !sameAuthored(*entry.before, entry.after))
                    {
                        changes.push_back(std::move(entry));
                    }
                }
                const bool nameChanged = newName.has_value() && *newName != entity.name;
                const bool activeChanged = active.has_value() && *active != entity.isActive;

                std::vector<RectMove> moves;
                if (moveDescendants)
                {
                    if (const StagedSection* rect = findStaged(changes, Section::Rect); rect != nullptr && rect->before)
                    {
                        const utilities::ui::AffineDelta delta = utilities::ui::centreDelta(
                            utilities::ui::affineRectOf(utilities::ui::toRectLayout(
                                std::get<services::UIRectData>(*rect->before))),
                            utilities::ui::affineRectOf(utilities::ui::toRectLayout(
                                std::get<services::UIRectData>(rect->after))));
                        if (delta.frac != glm::vec2(0.0f) || delta.offset != glm::vec2(0.0f))
                        {
                            moves = planDescendantMoves(entity, delta);
                        }
                    }
                }

                if (changes.empty() && !nameChanged && !activeChanged)
                {
                    nlohmann::json out{
                        {"entity", toId(entity.handle)},
                        {"changed", false},
                        {"changes", nlohmann::json::array()},
                        {"widget", current},
                        {"movedDescendants", 0},
                        {"warnings", warnings}
                    };
                    return reply(std::move(out), "Unchanged: UI entity " + describe(entity) + " already has these values.");
                }

                // ---- Warnings ----
                if (findStaged(changes, Section::Rect) != nullptr && current.value("layoutControlled", false))
                {
                    warnings.push_back("The parent's layout group rewrites this widget's position every frame (in Play).");
                }
                if (current.contains("styleKey"))
                {
                    warnings.push_back("The widget has a style key: a canvas theme re-applies its style on every scene "
                                       "load and may overwrite colours, fonts and textures set here.");
                }

                // ---- Apply: one batch, rolled back on a failure part-way ----
                const uint32_t id = toId(entity.handle);
                auto& dispatcher = events::EventDispatcher::instance();
                undo::UndoRecorder recorder("MCP: Set UI (" + entity.name + ")");
                nlohmann::json changedKeys = nlohmann::json::array();
                try
                {
                    for (StagedSection& change : changes)
                    {
                        const std::string key(sectionKey(change.section));
                        if (!change.before.has_value())
                        {
                            const std::vector<Section> presentBefore = presentSections(entity.handle);
                            addSection(entity.handle, change.section);
                            std::vector<Section> added;  // the section and any sibling the engine auto-added
                            for (const Section section : presentSections(entity.handle))
                            {
                                if (!contains(presentBefore, section))
                                {
                                    added.push_back(section);
                                }
                            }
                            std::vector<undo::UISectionChange> attachedChanges;
                            try
                            {
                                for (const Section section : added)
                                {
                                    const std::optional<SectionData> live = readSection(entity.handle, section);
                                    if (!live.has_value())
                                    {
                                        continue;
                                    }
                                    SectionData after = *live;
                                    if (section == change.section)
                                    {
                                        after = copyAuthored(*live, change.after);
                                        writeSection(entity.handle, after);
                                    }
                                    attachedChanges.push_back({section, std::nullopt, std::move(after)});
                                }
                            }
                            catch (...)
                            {
                                // Not recorded yet, so rollback() cannot take the attach back: remove what it added
                                // (in reverse order, so an auto-added UIRect goes last).
                                for (auto it = added.rbegin(); it != added.rend(); ++it)
                                {
                                    try
                                    {
                                        removeSection(entity.handle, *it);
                                    }
                                    catch (const std::exception&)
                                    {
                                    }
                                }
                                throw;
                            }
                            recorder.add(std::make_unique<undo::UISectionUndo>(id, entity.name, std::move(attachedChanges)));
                            changedKeys.push_back(key);
                            continue;
                        }

                        const std::optional<SectionData> live = readSection(entity.handle, change.section);
                        if (!live.has_value())
                        {
                            throw std::runtime_error("Entity " + describe(entity) + " lost its " +
                                                     std::string(sectionComponents[static_cast<std::size_t>(change.section)]));
                        }
                        SectionData after = copyAuthored(*live, change.after);
                        writeSection(entity.handle, after);
                        std::vector<undo::UISectionChange> sectionChanges;
                        sectionChanges.push_back({change.section, *live, std::move(after)});
                        recorder.add(std::make_unique<undo::UISectionUndo>(id, entity.name, std::move(sectionChanges)));
                        changedKeys.push_back(key);
                    }

                    if (nameChanged)
                    {
                        events::scene::SetEntityNameCommand rename;
                        rename.entity = entity.handle;
                        rename.newName = *newName;
                        dispatcher.execute(rename);
                        recorder.add(std::make_unique<undo::RenameUndo>(id, entity.name, *newName));
                        changedKeys.push_back("name");
                    }
                    if (activeChanged)
                    {
                        events::scene::SetEntityActiveCommand show;
                        show.entity = entity.handle;
                        show.isActive = *active;
                        dispatcher.execute(show);
                        recorder.add(std::make_unique<undo::ActiveUndo>(id, newName.value_or(entity.name), entity.isActive,
                                                                        *active));
                        changedKeys.push_back("active");
                    }
                    for (const RectMove& move : moves)
                    {
                        applyRectMove(move);
                        std::vector<undo::UISectionChange> moveChanges;
                        moveChanges.push_back({Section::Rect, SectionData{move.before}, SectionData{move.after}});
                        recorder.add(std::make_unique<undo::UISectionUndo>(toId(move.entity.handle), move.entity.name,
                                                                          std::move(moveChanges)));
                    }
                }
                catch (...)
                {
                    recorder.rollback();
                    throw;
                }
                recorder.push();

                const std::optional<services::EntityData> fresh = findEntity(entity.handle);
                nlohmann::json out{
                    {"entity", id},
                    {"changed", true},
                    {"changes", std::move(changedKeys)},
                    {"widget", fresh.has_value() ? widgetReport(*fresh, {}) : nlohmann::json(nullptr)},
                    {"movedDescendants", static_cast<int64_t>(moves.size())},
                    {"warnings", warnings}
                };
                std::string summary = "Updated UI entity " + std::to_string(id) + " ('" + newName.value_or(entity.name) +
                                      "'): " + out["changes"].dump() + ".";
                if (!moves.empty())
                {
                    summary += " Moved " + std::to_string(moves.size()) + " UI entit" + (moves.size() == 1 ? "y" : "ies") +
                               " below it along.";
                }
                for (const std::string& warning : warnings)
                {
                    summary += " Warning: " + warning;
                }
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }
    }

    void registerUIEditTools(ToolRegistry& registry, const ToolContext&)
    {
        registerGet(registry);
        registerSet(registry);
    }
}
