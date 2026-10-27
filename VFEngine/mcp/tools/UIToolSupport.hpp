#pragma once

#include "SettingsTable.hpp"

#include "data/DTOs.hpp"
#include "data/EntityHandle.hpp"
#include "ui/UIRectLayout.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// VK-1659 helpers shared by the UI tools (UITools.cpp, UIEditTools.cpp, UIPrefabTools.cpp, UIWidgets.cpp) and
// their undo command (mcp/undo/UIUndo).
//
// A UI entity is edited section by section: one section per UI component, holding that component's DTO
// (services/data/DTOs.hpp). The section tables (env::SectionTable, SettingsTable.hpp) are the single source of
// every key, kind and range ui_get reports, ui_set accepts, ui_list_widget_types lists and the docs describe.
// Keys are the DTO member names; enum values are lowerCamel names matched case-insensitively; asset fields are
// project-relative paths ("" = none). Runtime members of a DTO (currentState, isOpen, displayValue, ...) are
// reported read-only and are never written back over the engine's live value (copyAuthored).
//
// Every function that dispatches runs on the main thread (inside a Main-affinity handler or an undo command);
// the others say "Any thread". MCP code never touches EnTT: everything goes through EventDispatcher.
namespace mcp::tools::uitool
{
    // ------------------------------------------------------------------
    // Sections (any thread)
    // ------------------------------------------------------------------

    enum class Section : uint8_t
    {
        Canvas,
        Rect,
        Image,
        Label,
        Button,
        Checkbox,
        Slider,
        ProgressBar,
        Dropdown,
        TextInput,
        LayoutGroup,
        ListView,
        Mask,
        Scroll
    };
    inline constexpr std::size_t sectionCount = 14;

    // The argument / report key of each section, in Section order.
    inline constexpr std::array<std::string_view, sectionCount> sectionKeys{
        "canvas", "rect", "image", "label", "button", "checkbox", "slider",
        "progressBar", "dropdown", "textInput", "layoutGroup", "listView", "mask", "scroll"};
    // The engine component behind each section, as scene_get_hierarchy names it.
    inline constexpr std::array<std::string_view, sectionCount> sectionComponents{
        "UICanvas", "UIRect", "UIImage", "UILabel", "UIButton", "UICheckbox", "UISlider",
        "UIProgressBar", "UIDropdown", "UITextInput", "UILayoutGroup", "UIListView", "UIMask", "UIScroll"};

    inline std::string_view sectionKey(Section section)
    {
        return sectionKeys[static_cast<std::size_t>(section)];
    }
    // Exact (case-sensitive) match against sectionKeys, like every argument key.
    std::optional<Section> sectionFromKey(std::string_view key);

    // The sections ui_set may attach to an entity that lacks them: image, label, layoutGroup, mask, scroll.
    bool isAttachable(Section section);

    // The canvas section: UICanvasData plus the canvas' theme, which is reported read-only (a theme writes into
    // every styled widget and is re-applied on each scene load, so it is not an MCP edit).
    struct CanvasValues
    {
        services::UICanvasData data;
        std::string theme;  // project-relative .vfTheme path (GetCanvasThemeQuery), "" = none
    };

    // Alternative index == static_cast<std::size_t>(Section).
    using SectionData = std::variant<CanvasValues, services::UIRectData, services::UIImageData, services::UILabelData,
                                     services::UIButtonData, services::UICheckboxData, services::UISliderData,
                                     services::UIProgressBarData, services::UIDropdownData, services::UITextInputData,
                                     services::UILayoutGroupData, services::UIListViewData, services::UIMaskData,
                                     services::UIScrollData>;

    inline Section sectionOf(const SectionData& data)
    {
        return static_cast<Section>(data.index());
    }

    // The DTO default (what Add*Component creates, before any auto-add tweak).
    SectionData defaultSection(Section section);

    // ------------------------------------------------------------------
    // Engine access (main thread)
    // ------------------------------------------------------------------

    // Has*ComponentQuery.
    bool hasSection(const services::EntityHandle& entity, Section section);
    // Every section the entity has, in Section order.
    std::vector<Section> presentSections(const services::EntityHandle& entity);
    // Get*DataQuery (the canvas adds GetCanvasThemeQuery); nullopt when the entity lacks the component.
    std::optional<SectionData> readSection(const services::EntityHandle& entity, Section section);
    // Set*DataCommand with `data` as given (the canvas theme is never written). Throws std::runtime_error when
    // the engine refuses. Callers send copyAuthored(live, staged) so runtime state survives.
    void writeSection(const services::EntityHandle& entity, const SectionData& data);
    // Add*ComponentCommand: the engine auto-adds sibling components (Button -> Image + Label, LayoutGroup ->
    // Children, ...). Throws std::runtime_error when refused (e.g. already present).
    void addSection(const services::EntityHandle& entity, Section section);
    // Remove*ComponentCommand. Throws std::runtime_error when refused.
    void removeSection(const services::EntityHandle& entity, Section section);

    // ------------------------------------------------------------------
    // Values
    // ------------------------------------------------------------------

    // The section as ui_get reports it and ui_set accepts it back: {key: value}, read-only keys included.
    // Main thread (asset fields are resolved against the project).
    nlohmann::json sectionJson(const SectionData& data);
    // sectionJson without the read-only keys: what "changed" compares.
    nlohmann::json authoredJson(const SectionData& data);
    // `base` with `patch` applied, all-or-nothing: keys already holding their current value are dropped first
    // (env::dropUnchanged, so any reported value passes back, read-only keys included); then env::strictPatch
    // (unknown key, wrong type, out-of-range value, read-only key set to a new value, cross-check failure ->
    // ArgError naming '<prefix>.<key>'). Asset fields check the file. Main thread.
    SectionData patchSection(const SectionData& base, const nlohmann::json& patch, std::string_view prefix);
    // `live` with every authored member taken from `authored`; runtime members (currentState, isOpen,
    // hoveredOptionIndex, isDragging, displayValue, selectedIndex of a list view, externalTextureKey,
    // renderTextureSource) stay as `live` has them. Both must be the same section. Any thread.
    SectionData copyAuthored(const SectionData& live, const SectionData& authored);
    // authoredJson(a) == authoredJson(b). Main thread.
    bool sameAuthored(const SectionData& a, const SectionData& b);
    // {key: {type, min, max | values, readOnly?, extension?}} (env::rangesJson). Any thread.
    nlohmann::json sectionRanges(Section section);
    // A JSON-Schema object of the section's keys: settable keys with their kinds and ranges, read-only keys
    // described as such. Any thread.
    nlohmann::json sectionSchema(Section section, std::string description);

    // ------------------------------------------------------------------
    // Widget types (any thread)
    // ------------------------------------------------------------------

    enum class WidgetType : uint8_t
    {
        Rect,
        Image,
        Label,
        Button,
        Checkbox,
        Slider,
        ProgressBar,
        Dropdown,
        ListView,
        TextInput,
        LayoutGroup
    };
    inline constexpr std::array<std::string_view, 11> widgetTypeNames{
        "Rect", "Image", "Label", "Button", "Checkbox", "Slider",
        "ProgressBar", "Dropdown", "ListView", "TextInput", "LayoutGroup"};

    // Case-insensitive.
    std::optional<WidgetType> widgetTypeFromName(std::string_view name);

    // The type ui_get reports, by priority: "Canvas", "Button", "Checkbox", "Slider", "ProgressBar", "Dropdown",
    // "TextInput", "ListView", "Tabs", "Window", "LayoutGroup", "Label", "Image", "Rect"; "" without any UI
    // component. From EntityData::components (UIMask has no ComponentTypeId and never decides the type).
    std::string detectType(const services::EntityData& entity);

    // ------------------------------------------------------------------
    // Hierarchy (main thread)
    // ------------------------------------------------------------------

    // Every UI component the entity carries, by component name (EntityData::components plus UIMask through
    // HasUIMaskComponentQuery), in a fixed order.
    std::vector<std::string> uiComponentNames(const services::EntityData& entity);
    // Carries any UI component (UIMask included).
    bool isUIEntity(const services::EntityData& entity);
    // The entity itself when it carries a UICanvas, else its nearest UICanvas ancestor; nullopt when none.
    std::optional<services::EntityData> findCanvas(const services::EntityData& entity);
    // Some ancestor (not the entity itself) is a UI entity.
    bool hasUIAncestor(const services::EntityData& entity);
    // IsUIListItemQuery: an engine-managed list item instance. False when no handler is registered.
    bool isListItem(const services::EntityHandle& entity);
    // IsInUIPreviewSandboxQuery: in a UI Layer Builder preview sandbox (the builder's editor-only working copy, never
    // drawn in the scene nor saved with it). False when no handler is registered.
    bool inPreviewSandbox(const services::EntityHandle& entity);
    // A checkbox's label part: its first child carrying a UILabel (what labelToggle hit-tests); nullopt for an
    // entity without a UICheckbox or without such a child.
    std::optional<services::EntityHandle> checkboxLabelPart(const services::EntityData& checkbox);
    // FindEntitiesByNameQuery: how many entities are named `name` (scripts' Entity::findByName takes the first).
    std::size_t countEntitiesNamed(const std::string& name);

    // ------------------------------------------------------------------
    // Placement
    // ------------------------------------------------------------------

    struct PlacementArgs
    {
        utilities::ui::Placement placement;
        bool relativeToCanvas = false;  // relativeTo "canvas"; the default "parent" places inside the parent widget
    };

    // A `placement` argument object: preset (one of utilities::ui::anchorPresets, case-insensitive; default
    // "center"), size [w, h] (reference px, >= 0; default `defaultSize`), margin (n | [x, y] | {left, right, top,
    // bottom}; reference px, >= 0; default 0), offset [dx, dy] (reference px, +y DOWN; default 0), relativeTo
    // "parent" | "canvas". ArgError naming '<path>.<key>' for anything else. Any thread.
    PlacementArgs readPlacement(const nlohmann::json& value, glm::vec2 defaultSize, std::string_view path);
    // The JSON-Schema of a placement object. Any thread.
    nlohmann::json placementSchema(std::string description);
    // The edges a placement is relative to: the whole canvas for a canvas root, a parent without a UIRect, or
    // `relativeToCanvas`; otherwise the parent's UIRect (affineRectOf). Main thread.
    utilities::ui::AffineRect placementFrame(const services::EntityData& parent, bool relativeToCanvas);
    // The rect a placement produces inside `frame` (utilities::ui::placeInside, pivot 0.5); blocksRaycast is
    // taken from `current`. Any thread.
    services::UIRectData placeRect(const services::UIRectData& current, const PlacementArgs& args,
                                   const utilities::ui::AffineRect& frame);

    // ------------------------------------------------------------------
    // Reports (main thread)
    // ------------------------------------------------------------------

    // {x, y, w, h, viewport: [w, h], scale}: utilities::ui::resolveRect in resolved pixels (top-left origin, y
    // down) for `viewport` (default: the canvas reference size), at the canvas scale (scaleWithScreenSize:
    // min(vw / refW, vh / refH); constantPixelSize: 1). Any thread.
    nlohmann::json resolvedJson(const services::UIRectData& rect, const services::UICanvasData& canvas,
                                std::optional<glm::vec2> viewport);

    struct ReportOptions
    {
        bool ranges = false;                // add "ranges": {section: sectionRanges} for the present sections
        std::optional<glm::vec2> viewport;  // the viewport of "resolved"; default the canvas reference size
    };

    // ui_get's report of one UI entity:
    //   entity, name, type, active, effectivelyActive, parent, canvasId, children (ids), parts ({label: id} for a
    //   checkbox, else {}), layoutControlled (the parent has a UILayoutGroup: the layout rewrites the position),
    //   listItem, components (uiComponentNames), one key per present section (sectionJson), styleKey (when the
    //   entity has a UIStyle), resolved (when it has a UIRect and a canvas), validation ({ok, errors, warnings},
    //   only when not ok), ranges (options.ranges).
    // ui_set accepts the whole report back: entity is its own argument, name and active are settable, the
    // sections patch, and the keys below are read-only.
    nlohmann::json widgetReport(const services::EntityData& entity, const ReportOptions& options);

    // Report keys ui_set accepts back only when they equal the current report.
    inline constexpr std::array<std::string_view, 10> reportIdentityKeys{
        "type", "effectivelyActive", "parent", "canvasId", "children", "parts",
        "layoutControlled", "listItem", "components", "styleKey"};
    // Report keys ui_set accepts back and ignores: they describe the entity and never configure it.
    inline constexpr std::array<std::string_view, 3> reportDerivedKeys{"resolved", "validation", "ranges"};
}
