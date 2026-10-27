#pragma once

#include "CoreTools.hpp"
#include "UIToolSupport.hpp"

#include "data/DTOs.hpp"
#include "data/EntityHandle.hpp"
#include "ui/UIRectLayout.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// VK-1659: the ui_add_widget recipes (what each widget type creates, its starter style and default size) and the
// helpers the three UI tool TUs share (UITools.cpp, UIEditTools.cpp, UIPrefabTools.cpp). The sections themselves
// (keys, ranges, engine access) are UIToolSupport's. Functions that dispatch run on the main thread; the others
// say "Any thread".
namespace mcp::tools::uitool
{
    // ------------------------------------------------------------------
    // Recipes (any thread)
    // ------------------------------------------------------------------

    struct WidgetRecipe
    {
        WidgetType type = WidgetType::Rect;
        std::string_view description;
        // The component ui_add_widget adds first; the engine auto-adds its siblings (UIComponentService Add*).
        Section primary = Section::Rect;
        // Every section the widget ends up with (primary, the engine's auto-adds and the recipe's extras), in
        // Section order.
        std::vector<Section> sections;
        glm::vec2 defaultSize{100.0f, 100.0f};  // reference px
        // What the `text` argument sets ("" = the type takes no text).
        std::string_view textTarget;
        // Child parts ui_add_widget creates ("" = none).
        std::string_view parts;
        // A short scripting hint (the UI:: calls and listener that drive the type).
        std::string_view scriptHint;
    };

    // The 11 recipes, in WidgetType order.
    const std::vector<WidgetRecipe>& widgetRecipes();
    const WidgetRecipe& recipeOf(WidgetType type);

    // Every section of a widget of `type`, staged: the DTO default, the engine's auto-add values and the recipe's
    // starter style (no `text`, no caller values). Keyed by Section.
    std::map<Section, SectionData> starterSections(WidgetType type);

    // The starter style as {sectionKey: {key: value}}: the authored keys whose starter value differs from the DTO
    // default. Main thread (sectionJson resolves asset fields).
    nlohmann::json starterStyleJson(WidgetType type);

    // The text a widget shows by default when no `text` argument is given (the Checkbox part shows its name).
    std::string defaultText(WidgetType type, const std::string& widgetName);

    // Applies `text` to the staged sections the way the type's textTarget says. Throws ArgError for a type that
    // takes no text. Any thread.
    void applyText(WidgetType type, std::map<Section, SectionData>& staged, const std::string& text);

    // Checkbox part: the label child's staged label section (white, 24 px, left / middle, `text`) and its rect,
    // placed {Left, Middle} 12 px right of the box, 260 x 40 reference px. Any thread.
    services::UILabelData checkboxPartLabel(const std::string& text);
    services::UIRectData checkboxPartRect(const services::UIRectData& box);
    inline constexpr std::string_view checkboxPartSuffix = "Label";

    // ui_list_widget_types' entry of one recipe: {type, description, components, sections, defaultSize,
    // starterStyle, text, parts, script}. Main thread.
    nlohmann::json recipeJson(const WidgetRecipe& recipe);

    // ------------------------------------------------------------------
    // Shared tool helpers (any thread)
    // ------------------------------------------------------------------

    // Why ui_create_canvas, ui_add_widget and ui_instantiate_prefab refuse in World mode.
    inline constexpr const char* worldModeRefusal =
        "Cannot create UI in World mode: every new entity there is assigned to a streaming sector by its position, "
        "so a HUD would unload with its sector. Build UI in an ordinary scene.";

    // The section keys joined with ", " ("none" when empty), for messages.
    std::string sectionList(const std::vector<Section>& sections);

    // Do the tool arguments set `<section>.<key>` (present and not null)?
    bool patchSets(const nlohmann::json& args, const char* section, const char* key);

    // What a dropdown's root label shows: the selected option, or the placeholder while nothing is selected (what
    // the runtime writes on a selection, UIInteractionSystemDropdown.cpp).
    std::string dropdownCaption(const services::UIDropdownData& dropdown);

    // ------------------------------------------------------------------
    // Shared tool helpers (main thread)
    // ------------------------------------------------------------------

    // "12 ('ScoreLabel')".
    std::string describe(const services::EntityData& entity);

    // Throws (runtime_error) when `entity`, given as the argument `name`, lies in a UI Layer Builder preview sandbox.
    void rejectPreviewSandbox(const services::EntityData& entity, const char* name);

    // The entity argument `name`, which must exist, carry a UI component and lie outside a UI Layer Builder preview
    // sandbox (ArgError / runtime_error otherwise).
    services::EntityData requireUIEntity(const nlohmann::json& args, const char* name);

    // The canvas data of `entity`'s canvas (itself or an ancestor); nullopt without a canvas.
    std::optional<services::UICanvasData> canvasDataOf(const services::EntityData& entity);

    // Every live descendant of `root` (not root itself) in pre-order, list items and their subtrees skipped
    // (the engine rebuilds them); at most `limit` entries. `truncated` is set when the limit cut the walk.
    std::vector<services::EntityData> uiDescendants(const services::EntityData& root, std::size_t limit, bool* truncated);

    // A planned rect move of one entity (moveDescendants).
    struct RectMove
    {
        services::EntityData entity;
        services::UIRectData before;
        services::UIRectData after;
    };

    // The rect moves that translate every UI descendant of `root` (with a UIRect) by `delta`; nothing is written.
    std::vector<RectMove> planDescendantMoves(const services::EntityData& root, const utilities::ui::AffineDelta& delta);

    // Writes one planned move (copyAuthored over the live rect). Throws std::runtime_error when the engine refuses.
    void applyRectMove(const RectMove& move);

    // The current size of `rect` in reference px (resolveRect at the canvas reference size, scale 1).
    glm::vec2 referenceRectSize(const services::UIRectData& rect, const services::UICanvasData& canvas);

    // Deletes `entity` (DeleteEntityCommand), swallowing failures: the cleanup of a half-built creation.
    void deleteQuietly(const services::EntityHandle& entity);

    // ------------------------------------------------------------------
    // Registration of the other UI tool TUs (called by registerUITools in UITools.cpp)
    // ------------------------------------------------------------------

    void registerUIEditTools(ToolRegistry& registry, const ToolContext& context);    // ui_get, ui_set
    void registerUIPrefabTools(ToolRegistry& registry, const ToolContext& context);  // ui_save_prefab, ui_instantiate_prefab, ui_click
}
