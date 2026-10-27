#include "UIWidgets.hpp"
#include "ToolHelpers.hpp"
#include "../protocol/ArgReader.hpp"

#include "events/EventDispatcher.hpp"
#include "events/scene/EntityTransformEvents.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mcp::tools::uitool
{
    namespace
    {
        // Label alignment / layout direction codes (services::UILabelData, UILayoutGroupData).
        constexpr uint8_t alignLeft = 0;
        constexpr uint8_t alignCenter = 1;
        constexpr uint8_t alignMiddle = 1;
        constexpr uint8_t layoutVertical = 0;

        // Starter colours: a dark translucent panel and the button blues (plan VK-1659 §4.3).
        constexpr glm::vec4 panelTint{0.10f, 0.10f, 0.12f, 0.85f};
        constexpr glm::vec4 white{1.0f, 1.0f, 1.0f, 1.0f};
        constexpr glm::vec4 buttonNormal{0.16f, 0.42f, 0.86f, 1.0f};
        constexpr glm::vec4 buttonHovered{0.24f, 0.52f, 0.96f, 1.0f};
        constexpr glm::vec4 buttonPressed{0.10f, 0.30f, 0.66f, 1.0f};
        constexpr glm::vec4 buttonDisabled{0.35f, 0.35f, 0.38f, 0.6f};
        constexpr glm::vec4 checkboxUnchecked{0.85f, 0.85f, 0.88f, 1.0f};
        constexpr glm::vec4 checkboxChecked{0.20f, 0.70f, 0.35f, 1.0f};
        constexpr glm::vec4 sliderTrack{0.15f, 0.15f, 0.18f, 1.0f};
        constexpr glm::vec4 sliderFill{0.16f, 0.55f, 0.95f, 1.0f};
        constexpr glm::vec4 progressTrack{0.10f, 0.10f, 0.12f, 0.85f};
        constexpr glm::vec4 progressFill{0.20f, 0.80f, 0.30f, 1.0f};

        // Checkbox part geometry, reference px.
        constexpr float partGap = 12.0f;
        constexpr float partWidth = 260.0f;
        constexpr float partHeight = 40.0f;

        template <typename T>
        T& staged(std::map<Section, SectionData>& sections, Section section)
        {
            return std::get<T>(sections.at(section));
        }

        std::vector<WidgetRecipe> makeRecipes()
        {
            std::vector<WidgetRecipe> recipes;
            recipes.reserve(widgetTypeNames.size());

            recipes.push_back({WidgetType::Rect,
                               "An invisible container (a UIRect only): groups widgets, holds a layout group or a mask "
                               "(attach them with ui_set).",
                               Section::Rect, {Section::Rect}, {400.0f, 300.0f}, "", "", ""});
            recipes.push_back({WidgetType::Image,
                               "A coloured panel or a picture (textureRef, a .vfImage); a dark translucent panel by "
                               "default, white when a texture is given.",
                               Section::Image, {Section::Rect, Section::Image}, {256.0f, 256.0f}, "", "",
                               "UI::setImageColor / UI::setImageTexture"});
            recipes.push_back({WidgetType::Label,
                               "Text (white, 28 px, left / middle). Scripts set it with UI::setLabelText.",
                               Section::Label, {Section::Rect, Section::Label}, {320.0f, 48.0f}, "label.text", "",
                               "UI::setLabelText(id, text); numbers need parsePrimitive"});
            recipes.push_back({WidgetType::Button,
                               "A clickable button: its own image (the state colour) and its own label (white, 24 px, "
                               "centred) on ONE entity. Colour it with button.normalColor / hoveredColor / "
                               "pressedColor - the state colour overwrites image.colorTint every frame.",
                               Section::Button, {Section::Rect, Section::Image, Section::Label, Section::Button},
                               {200.0f, 60.0f}, "label.text", "",
                               "a script implementing IUIButtonListener gets onButtonClicked(id, name) for EVERY button; "
                               "compare the name"});
            recipes.push_back({WidgetType::Checkbox,
                               "A check box (the root, coloured by checkbox.uncheckedColor / checkedColor) with a "
                               "clickable text label child to its right (labelToggle).",
                               Section::Checkbox, {Section::Rect, Section::Image, Section::Checkbox}, {32.0f, 32.0f},
                               "the label part's label.text",
                               "label: the child '<name>Label' (UIRect + UILabel), 12 px right of the box, 260 x 40",
                               "IUICheckboxListener.onCheckboxToggled(id, name, newState, previousState); "
                               "UI::isCheckboxChecked / UI::setCheckboxChecked"});
            recipes.push_back({WidgetType::Slider,
                               "A draggable value (slider.minValue..maxValue); the image is the track, the engine draws "
                               "the fill (slider.fillColor) and the handle.",
                               Section::Slider, {Section::Rect, Section::Image, Section::Slider}, {300.0f, 24.0f}, "", "",
                               "IUISliderListener.onSliderValueChanged; UI::getSliderValue / UI::setSliderValue"});
            recipes.push_back({WidgetType::ProgressBar,
                               "A bar filled to progressBar.value between minValue and maxValue (0..1 by default, full); "
                               "the engine draws the track and the fill.",
                               Section::ProgressBar, {Section::Rect, Section::ProgressBar}, {320.0f, 28.0f}, "", "",
                               "UI::setProgressBarValue(id, value) (clamped to min..max); UI::setProgressBarMinMax"});
            recipes.push_back({WidgetType::Dropdown,
                               "A drop-down list of dropdown.options; its own label shows the selection (or the "
                               "placeholder).",
                               Section::Dropdown, {Section::Rect, Section::Image, Section::Label, Section::Dropdown},
                               {240.0f, 40.0f}, "dropdown.placeholderText (and the label while nothing is selected)", "",
                               "IUIDropdownListener.onDropdownSelectionChanged; UI::getDropdownSelectedIndex"});
            recipes.push_back({WidgetType::ListView,
                               "Repeated rows: listView.itemCount instances of the listView.itemTemplateRef .vfPrefab "
                               "(make a row with ui_add_widget, then ui_save_prefab; a single-entity row is best), laid "
                               "out by its vertical layout group. The rows are engine-managed and never saved.",
                               Section::ListView, {Section::Rect, Section::Image, Section::LayoutGroup, Section::ListView},
                               {320.0f, 400.0f}, "", "",
                               "UI::setListItemCount / UI::getListItem(id, index); IUIListViewListener"});
            recipes.push_back({WidgetType::TextInput,
                               "A one-line text field (textInput.text; placeholderText while empty).",
                               Section::TextInput, {Section::Rect, Section::Image, Section::TextInput}, {320.0f, 44.0f},
                               "textInput.placeholderText", "",
                               "IUITextInputListener.onTextInputSubmit; UI::getTextInputText"});
            recipes.push_back({WidgetType::LayoutGroup,
                               "An invisible container that lines up its DIRECT children (vertical, horizontal or grid; "
                               "spacing / padding in unscaled pixels) every frame in Play.",
                               Section::LayoutGroup, {Section::Rect, Section::LayoutGroup}, {320.0f, 400.0f}, "", "", ""});
            return recipes;
        }
    }

    // ------------------------------------------------------------------
    // Recipes
    // ------------------------------------------------------------------

    const std::vector<WidgetRecipe>& widgetRecipes()
    {
        static const std::vector<WidgetRecipe> recipes = makeRecipes();
        return recipes;
    }

    const WidgetRecipe& recipeOf(WidgetType type)
    {
        return widgetRecipes().at(static_cast<std::size_t>(type));
    }

    std::map<Section, SectionData> starterSections(WidgetType type)
    {
        std::map<Section, SectionData> sections;
        for (const Section section : recipeOf(type).sections)
        {
            sections.emplace(section, defaultSection(section));
        }

        switch (type)
        {
        case WidgetType::Rect:
            break;
        case WidgetType::Image:
            staged<services::UIImageData>(sections, Section::Image).colorTint = panelTint;
            break;
        case WidgetType::Label:
        {
            auto& label = staged<services::UILabelData>(sections, Section::Label);
            label.color = white;
            label.fontSize = 28.0f;
            label.horizontalAlignment = alignLeft;
            label.verticalAlignment = alignMiddle;
            break;
        }
        case WidgetType::Button:
        {
            auto& button = staged<services::UIButtonData>(sections, Section::Button);
            button.normalColor = buttonNormal;
            button.hoveredColor = buttonHovered;
            button.pressedColor = buttonPressed;
            button.disabledColor = buttonDisabled;
            staged<services::UIImageData>(sections, Section::Image).colorTint = buttonNormal;
            // What UIComponentService::addUIButtonComponent seeds the auto-added label with, plus the starter look.
            auto& label = staged<services::UILabelData>(sections, Section::Label);
            label.text = "Button";
            label.horizontalAlignment = alignCenter;
            label.verticalAlignment = alignMiddle;
            label.color = white;
            label.fontSize = 24.0f;
            break;
        }
        case WidgetType::Checkbox:
        {
            auto& checkbox = staged<services::UICheckboxData>(sections, Section::Checkbox);
            checkbox.uncheckedColor = checkboxUnchecked;
            checkbox.checkedColor = checkboxChecked;
            checkbox.labelToggle = true;
            staged<services::UIImageData>(sections, Section::Image).colorTint = checkboxUnchecked;
            break;
        }
        case WidgetType::Slider:
            staged<services::UIImageData>(sections, Section::Image).colorTint = sliderTrack;
            staged<services::UISliderData>(sections, Section::Slider).fillColor = sliderFill;
            break;
        case WidgetType::ProgressBar:
        {
            auto& bar = staged<services::UIProgressBarData>(sections, Section::ProgressBar);
            bar.trackColor = progressTrack;
            bar.fillColor = progressFill;
            bar.value = 1.0f;
            break;
        }
        case WidgetType::Dropdown:
        {
            auto& dropdown = staged<services::UIDropdownData>(sections, Section::Dropdown);
            dropdown.options = {{"Option 1", {}}, {"Option 2", {}}, {"Option 3", {}}};
            staged<services::UIImageData>(sections, Section::Image).colorTint = dropdown.normalColor;
            // What UIComponentService::addUIDropdownComponent seeds the auto-added label with, plus the starter look.
            auto& label = staged<services::UILabelData>(sections, Section::Label);
            label.text = dropdown.placeholderText;
            label.horizontalAlignment = alignLeft;
            label.verticalAlignment = alignMiddle;
            label.color = white;
            label.fontSize = 20.0f;
            break;
        }
        case WidgetType::ListView:
        {
            staged<services::UIImageData>(sections, Section::Image).colorTint = panelTint;
            auto& layout = staged<services::UILayoutGroupData>(sections, Section::LayoutGroup);
            layout.direction = layoutVertical;
            layout.spacing = 4.0f;
            layout.padding = glm::vec4(4.0f);
            break;
        }
        case WidgetType::TextInput:
            staged<services::UIImageData>(sections, Section::Image).colorTint =
                staged<services::UITextInputData>(sections, Section::TextInput).normalColor;
            break;
        case WidgetType::LayoutGroup:
        {
            auto& layout = staged<services::UILayoutGroupData>(sections, Section::LayoutGroup);
            layout.direction = layoutVertical;
            layout.spacing = 8.0f;
            layout.padding = glm::vec4(8.0f);
            break;
        }
        }
        return sections;
    }

    nlohmann::json starterStyleJson(WidgetType type)
    {
        nlohmann::json out = nlohmann::json::object();
        for (const auto& [section, data] : starterSections(type))
        {
            if (section == Section::Rect)
            {
                continue; // the placement decides the rect
            }
            const nlohmann::json starter = authoredJson(data);
            const nlohmann::json defaults = authoredJson(defaultSection(section));
            nlohmann::json changed = nlohmann::json::object();
            for (auto it = starter.begin(); it != starter.end(); ++it)
            {
                if (!defaults.contains(it.key()) || defaults.at(it.key()) != it.value())
                {
                    changed[it.key()] = it.value();
                }
            }
            if (!changed.empty())
            {
                out[std::string(sectionKey(section))] = std::move(changed);
            }
        }
        return out;
    }

    std::string defaultText(WidgetType type, const std::string& widgetName)
    {
        switch (type)
        {
        case WidgetType::Label:
            return "Label";
        case WidgetType::Button:
            return "Button";
        case WidgetType::Checkbox:
            return widgetName;
        case WidgetType::Dropdown:
            return "Select...";
        case WidgetType::TextInput:
            return "Enter text...";
        default:
            return {};
        }
    }

    void applyText(WidgetType type, std::map<Section, SectionData>& sections, const std::string& text)
    {
        switch (type)
        {
        case WidgetType::Label:
        case WidgetType::Button:
            staged<services::UILabelData>(sections, Section::Label).text = text;
            return;
        case WidgetType::Checkbox:
            return; // the label part carries the text (checkboxPartLabel)
        case WidgetType::Dropdown:
            staged<services::UIDropdownData>(sections, Section::Dropdown).placeholderText = text;
            staged<services::UILabelData>(sections, Section::Label).text = text;
            return;
        case WidgetType::TextInput:
            staged<services::UITextInputData>(sections, Section::TextInput).placeholderText = text;
            return;
        default:
            throw ArgError("argument 'text' does not apply to a " + std::string(widgetTypeNames[static_cast<std::size_t>(type)]) +
                           " widget (text is the caption of a Label, Button, Checkbox, Dropdown or TextInput)");
        }
    }

    services::UILabelData checkboxPartLabel(const std::string& text)
    {
        services::UILabelData label = std::get<services::UILabelData>(defaultSection(Section::Label));
        label.text = text;
        label.color = white;
        label.fontSize = 24.0f;
        label.horizontalAlignment = alignLeft;
        label.verticalAlignment = alignMiddle;
        return label;
    }

    services::UIRectData checkboxPartRect(const services::UIRectData& box)
    {
        // Glued to the box: the label's edges take the box's anchor terms, so it stays beside the box at every
        // resolution (UIRectLayout.hpp placeInside does the same for a preset).
        const utilities::ui::AffineRect edges = utilities::ui::affineRectOf(utilities::ui::toRectLayout(box));
        utilities::ui::AffineRect label;
        label.left = {edges.right.f, edges.right.c + partGap};
        label.right = {edges.right.f, edges.right.c + partGap + partWidth};
        const utilities::ui::AffineEdge middle{(edges.top.f + edges.bottom.f) * 0.5f, (edges.top.c + edges.bottom.c) * 0.5f};
        label.top = {middle.f, middle.c - partHeight * 0.5f};
        label.bottom = {middle.f, middle.c + partHeight * 0.5f};

        services::UIRectData out;
        out.blocksRaycast = box.blocksRaycast;
        utilities::ui::applyRectLayout(out, utilities::ui::rectFromEdges(label));
        return out;
    }

    nlohmann::json recipeJson(const WidgetRecipe& recipe)
    {
        nlohmann::json components = nlohmann::json::array();
        nlohmann::json sections = nlohmann::json::array();
        for (const Section section : recipe.sections)
        {
            components.push_back(std::string(sectionComponents[static_cast<std::size_t>(section)]));
            sections.push_back(std::string(sectionKey(section)));
        }
        nlohmann::json out{
            {"type", std::string(widgetTypeNames[static_cast<std::size_t>(recipe.type)])},
            {"description", std::string(recipe.description)},
            {"components", std::move(components)},
            {"sections", std::move(sections)},
            {"defaultSize", nlohmann::json::array({recipe.defaultSize.x, recipe.defaultSize.y})},
            {"starterStyle", starterStyleJson(recipe.type)},
            {"text", recipe.textTarget.empty() ? nlohmann::json(nullptr) : nlohmann::json(std::string(recipe.textTarget))},
            {"parts", recipe.parts.empty() ? nlohmann::json(nullptr) : nlohmann::json(std::string(recipe.parts))}
        };
        if (!recipe.scriptHint.empty())
        {
            out["script"] = std::string(recipe.scriptHint);
        }
        return out;
    }

    // ------------------------------------------------------------------
    // Shared tool helpers
    // ------------------------------------------------------------------

    std::string describe(const services::EntityData& entity)
    {
        return std::to_string(toId(entity.handle)) + " ('" + entity.name + "')";
    }

    void rejectPreviewSandbox(const services::EntityData& entity, const char* name)
    {
        if (inPreviewSandbox(entity.handle))
        {
            throw std::runtime_error("Entity " + describe(entity) + " (argument '" + name + "') belongs to the UI Layer "
                                     "Builder's preview: the builder's editor-only working copy, never drawn in the scene "
                                     "nor saved with it. Work on the scene's UI (ui_get without 'entity' lists its "
                                     "canvases); a layer the builder saved is a .vfPrefab that ui_instantiate_prefab "
                                     "brings into the scene.");
        }
    }

    services::EntityData requireUIEntity(const nlohmann::json& args, const char* name)
    {
        const ArgReader reader(args);
        services::EntityData entity = requireEntity(reader, name);
        if (!isUIEntity(entity))
        {
            throw std::runtime_error("Entity " + describe(entity) + " (argument '" + name + "') is not a UI entity: it "
                                     "carries no UI component. ui_get without 'entity' lists the canvases; "
                                     "ui_create_canvas / ui_add_widget create UI.");
        }
        rejectPreviewSandbox(entity, name);
        return entity;
    }

    std::optional<services::UICanvasData> canvasDataOf(const services::EntityData& entity)
    {
        const std::optional<services::EntityData> canvas = findCanvas(entity);
        if (!canvas.has_value())
        {
            return std::nullopt;
        }
        const std::optional<SectionData> section = readSection(canvas->handle, Section::Canvas);
        if (!section.has_value())
        {
            return std::nullopt;
        }
        return std::get<CanvasValues>(*section).data;
    }

    std::vector<services::EntityData> uiDescendants(const services::EntityData& root, std::size_t limit, bool* truncated)
    {
        if (truncated != nullptr)
        {
            *truncated = false;
        }
        std::vector<services::EntityData> out;
        std::unordered_set<uint64_t> seen{root.handle.id};
        // Explicit stack, children pushed in reverse so they come off in hierarchy order (pre-order).
        std::vector<services::EntityHandle> pending(root.children.rbegin(), root.children.rend());
        while (!pending.empty())
        {
            const services::EntityHandle handle = pending.back();
            pending.pop_back();
            if (!seen.insert(handle.id).second || isListItem(handle))
            {
                continue; // a cycle, or an engine-managed list row (rebuilt from its template)
            }
            std::optional<services::EntityData> child = findEntity(handle);
            if (!child.has_value())
            {
                continue;
            }
            if (out.size() == limit)
            {
                if (truncated != nullptr)
                {
                    *truncated = true;
                }
                break;
            }
            pending.insert(pending.end(), child->children.rbegin(), child->children.rend());
            out.push_back(std::move(*child));
        }
        return out;
    }

    std::vector<RectMove> planDescendantMoves(const services::EntityData& root, const utilities::ui::AffineDelta& delta)
    {
        std::vector<RectMove> moves;
        for (services::EntityData& entity : uiDescendants(root, static_cast<std::size_t>(-1), nullptr))
        {
            const std::optional<SectionData> rect = readSection(entity.handle, Section::Rect);
            if (!rect.has_value())
            {
                continue;
            }
            RectMove move;
            move.before = std::get<services::UIRectData>(*rect);
            move.after = move.before;
            utilities::ui::applyRectLayout(move.after,
                                           utilities::ui::translateRect(utilities::ui::toRectLayout(move.before), delta));
            move.entity = std::move(entity);
            moves.push_back(std::move(move));
        }
        return moves;
    }

    void applyRectMove(const RectMove& move)
    {
        const std::optional<SectionData> live = readSection(move.entity.handle, Section::Rect);
        if (!live.has_value())
        {
            throw std::runtime_error("Entity " + describe(move.entity) + " lost its UIRect while moving it");
        }
        writeSection(move.entity.handle, copyAuthored(*live, SectionData{move.after}));
    }

    std::string sectionList(const std::vector<Section>& sections)
    {
        std::string out;
        for (const Section section : sections)
        {
            out += out.empty() ? "" : ", ";
            out += sectionKey(section);
        }
        return out.empty() ? std::string("none") : out;
    }

    bool patchSets(const nlohmann::json& args, const char* section, const char* key)
    {
        const auto it = args.find(section);
        return it != args.end() && it->is_object() && it->contains(key) && !it->at(key).is_null();
    }

    std::string dropdownCaption(const services::UIDropdownData& dropdown)
    {
        if (dropdown.selectedIndex >= 0 && dropdown.selectedIndex < static_cast<int>(dropdown.options.size()))
        {
            return dropdown.options[static_cast<std::size_t>(dropdown.selectedIndex)].text;
        }
        return dropdown.placeholderText;
    }

    glm::vec2 referenceRectSize(const services::UIRectData& rect, const services::UICanvasData& canvas)
    {
        const utilities::ui::PixelRect resolved = utilities::ui::resolveRect(
            utilities::ui::toRectLayout(rect), canvas.referenceWidth, canvas.referenceHeight, 1.0f);
        return {resolved.w, resolved.h};
    }

    void deleteQuietly(const services::EntityHandle& entity)
    {
        try
        {
            events::scene::DeleteEntityCommand command;
            command.entity = entity;
            events::EventDispatcher::instance().execute(command);
        }
        catch (const std::exception&)
        {
            // Cleanup of a failed creation: the original error is what the caller reports.
        }
    }
}
