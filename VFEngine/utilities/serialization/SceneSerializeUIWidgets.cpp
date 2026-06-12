#include "SceneSerialization.hpp"
#include "AssetRefSerializationHelper.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"

namespace serialization {

    // ---- Label ----

    json SceneSerialization::serializeUILabel(const components::UILabelComponent& label)
    {
        json j;
        if (!label.text.empty())
        {
            j["text"] = label.text;
        }
        if (label.fontRef.isValid())
        {
            writeAssetRef(j, "fontRef", label.fontRef);
        }
        j["fontSize"] = label.fontSize;
        j["fontStyle"] = fontStyleToString(label.fontStyle);
        j["color"] = writeVec4(label.color);
        j["horizontalAlignment"] = horizontalAlignmentToString(label.horizontalAlignment);
        j["verticalAlignment"] = verticalAlignmentToString(label.verticalAlignment);
        j["overflow"] = textOverflowToString(label.overflow);
        j["wordWrap"] = label.wordWrap;
        j["lineSpacing"] = label.lineSpacing;
        j["letterSpacing"] = label.letterSpacing;
        j["richText"] = label.richText;
        return j;
    }

    void SceneSerialization::deserializeUILabel(const json& j, components::UILabelComponent& label)
    {
        label.text = j.value("text", std::string("Label"));
        label.fontRef = readAssetRef(j, "fontRef", "fontPath");
        label.fontSize = j.value("fontSize", 16.0f);
        label.fontStyle = stringToFontStyle(j.value("fontStyle", "normal"));
        readVec4(j, "color", label.color);
        label.horizontalAlignment = stringToHorizontalAlignment(j.value("horizontalAlignment", "left"));
        label.verticalAlignment = stringToVerticalAlignment(j.value("verticalAlignment", "top"));
        label.overflow = stringToTextOverflow(j.value("overflow", "overflow"));
        label.wordWrap = j.value("wordWrap", true);
        label.lineSpacing = j.value("lineSpacing", 1.0f);
        label.letterSpacing = j.value("letterSpacing", 0.0f);
        label.richText = j.value("richText", false);
    }

    // ---- Tooltip ----

    json SceneSerialization::serializeUITooltip(const components::UITooltipComponent& tooltip)
    {
        json j;
        j["mode"] = static_cast<int>(tooltip.mode);
        if (!tooltip.text.empty())
        {
            j["text"] = tooltip.text;
        }
        j["showDelay"] = tooltip.showDelay;
        j["followCursor"] = tooltip.followCursor;
        j["offset"] = writeVec2(tooltip.offset);
        j["maxWidth"] = tooltip.maxWidth;
        j["backgroundColor"] = writeVec4(tooltip.backgroundColor);
        j["textColor"] = writeVec4(tooltip.textColor);
        if (tooltip.fontRef.isValid())
        {
            writeAssetRef(j, "fontRef", tooltip.fontRef);
        }
        j["fontSize"] = tooltip.fontSize;
        j["padding"] = writeVec4(tooltip.padding);
        j["enabled"] = tooltip.enabled;
        if (!tooltip.panelChildName.empty())
        {
            j["panelChildName"] = tooltip.panelChildName;
        }
        return j;
    }

    void SceneSerialization::deserializeUITooltip(const json& j, components::UITooltipComponent& tooltip)
    {
        tooltip.mode = static_cast<components::UITooltipMode>(j.value("mode", 0));
        tooltip.text = j.value("text", std::string());
        tooltip.showDelay = j.value("showDelay", 0.5f);
        tooltip.followCursor = j.value("followCursor", true);
        readVec2(j, "offset", tooltip.offset);
        tooltip.maxWidth = j.value("maxWidth", 280.0f);
        readVec4(j, "backgroundColor", tooltip.backgroundColor);
        readVec4(j, "textColor", tooltip.textColor);
        tooltip.fontRef = readAssetRef(j, "fontRef", "");
        tooltip.fontSize = j.value("fontSize", 14.0f);
        readVec4(j, "padding", tooltip.padding);
        tooltip.enabled = j.value("enabled", true);
        tooltip.panelChildName = j.value("panelChildName", std::string());
    }

    // ---- Button ----

    json SceneSerialization::serializeUIButton(const components::UIButtonComponent& button)
    {
        json j;

        j["normalColor"] = writeVec4(button.normalColor);
        j["hoveredColor"] = writeVec4(button.hoveredColor);
        j["pressedColor"] = writeVec4(button.pressedColor);
        j["disabledColor"] = writeVec4(button.disabledColor);

        if (button.normalTextureRef.isValid())
        {
            writeAssetRef(j, "normalTextureRef", button.normalTextureRef);
        }
        if (button.hoverTextureRef.isValid())
        {
            writeAssetRef(j, "hoverTextureRef", button.hoverTextureRef);
        }
        if (button.pressedTextureRef.isValid())
        {
            writeAssetRef(j, "pressedTextureRef", button.pressedTextureRef);
        }
        if (button.disabledTextureRef.isValid())
        {
            writeAssetRef(j, "disabledTextureRef", button.disabledTextureRef);
        }

        j["colorTransitionDuration"] = button.colorTransitionDuration;
        j["interactable"] = button.interactable;

        return j;
    }

    void SceneSerialization::deserializeUIButton(const json& j, components::UIButtonComponent& button)
    {
        readVec4(j, "normalColor", button.normalColor);
        readVec4(j, "hoveredColor", button.hoveredColor);
        readVec4(j, "pressedColor", button.pressedColor);
        readVec4(j, "disabledColor", button.disabledColor);

        button.normalTextureRef = readAssetRef(j, "normalTextureRef");
        button.hoverTextureRef = readAssetRef(j, "hoverTextureRef");
        button.pressedTextureRef = readAssetRef(j, "pressedTextureRef");
        button.disabledTextureRef = readAssetRef(j, "disabledTextureRef");

        button.colorTransitionDuration = j.value("colorTransitionDuration", 0.1f);
        button.interactable = j.value("interactable", true);
    }

    // ---- TextInput ----

    json SceneSerialization::serializeUITextInput(const components::UITextInputComponent& textInput)
    {
        json j;

        j["text"] = textInput.text;
        j["placeholderText"] = textInput.placeholderText;

        if (textInput.fontRef.isValid())
        {
            writeAssetRef(j, "fontRef", textInput.fontRef);
        }

        j["fontSize"] = textInput.fontSize;
        j["textColor"] = writeVec4(textInput.textColor);
        j["placeholderColor"] = writeVec4(textInput.placeholderColor);
        j["normalColor"] = writeVec4(textInput.normalColor);
        j["hoveredColor"] = writeVec4(textInput.hoveredColor);
        j["focusedColor"] = writeVec4(textInput.focusedColor);
        j["disabledColor"] = writeVec4(textInput.disabledColor);

        j["colorTransitionDuration"] = textInput.colorTransitionDuration;
        j["interactable"] = textInput.interactable;
        j["maxLength"] = textInput.maxLength;

        j["selectionColor"] = writeVec4(textInput.selectionColor);
        j["caretColor"] = writeVec4(textInput.caretColor);
        j["caretWidth"] = textInput.caretWidth;
        j["caretBlinkRate"] = textInput.caretBlinkRate;

        return j;
    }

    void SceneSerialization::deserializeUITextInput(const json& j, components::UITextInputComponent& textInput)
    {
        textInput.text = j.value("text", std::string(""));
        textInput.placeholderText = j.value("placeholderText", std::string("Enter text..."));
        textInput.fontRef = readAssetRef(j, "fontRef");
        textInput.fontSize = j.value("fontSize", 16.0f);

        readVec4(j, "textColor", textInput.textColor);
        readVec4(j, "placeholderColor", textInput.placeholderColor);
        readVec4(j, "normalColor", textInput.normalColor);
        readVec4(j, "hoveredColor", textInput.hoveredColor);
        readVec4(j, "focusedColor", textInput.focusedColor);
        readVec4(j, "disabledColor", textInput.disabledColor);
        readVec4(j, "selectionColor", textInput.selectionColor);
        readVec4(j, "caretColor", textInput.caretColor);

        textInput.colorTransitionDuration = j.value("colorTransitionDuration", 0.1f);
        textInput.interactable = j.value("interactable", true);
        textInput.maxLength = j.value("maxLength", 0);
        textInput.caretWidth = j.value("caretWidth", 2.0f);
        textInput.caretBlinkRate = j.value("caretBlinkRate", 0.53f);

        textInput.currentDisplayColor = textInput.normalColor;
    }

    // ---- Checkbox ----

    json SceneSerialization::serializeUICheckbox(const components::UICheckboxComponent& checkbox)
    {
        json j;

        j["isChecked"] = checkbox.isChecked;

        if (!checkbox.groupName.empty())
        {
            j["groupName"] = checkbox.groupName;
        }
        j["allowUncheck"] = checkbox.allowUncheck;

        j["uncheckedColor"] = writeVec4(checkbox.uncheckedColor);
        j["checkedColor"] = writeVec4(checkbox.checkedColor);
        j["hoveredColor"] = writeVec4(checkbox.hoveredColor);
        j["disabledColor"] = writeVec4(checkbox.disabledColor);

        if (checkbox.uncheckedTextureRef.isValid())
        {
            writeAssetRef(j, "uncheckedTextureRef", checkbox.uncheckedTextureRef);
        }
        if (checkbox.checkedTextureRef.isValid())
        {
            writeAssetRef(j, "checkedTextureRef", checkbox.checkedTextureRef);
        }
        if (checkbox.hoveredTextureRef.isValid())
        {
            writeAssetRef(j, "hoveredTextureRef", checkbox.hoveredTextureRef);
        }
        if (checkbox.disabledTextureRef.isValid())
        {
            writeAssetRef(j, "disabledTextureRef", checkbox.disabledTextureRef);
        }

        j["colorTransitionDuration"] = checkbox.colorTransitionDuration;
        j["interactable"] = checkbox.interactable;
        j["labelToggle"] = checkbox.labelToggle;

        return j;
    }

    void SceneSerialization::deserializeUICheckbox(const json& j, components::UICheckboxComponent& checkbox)
    {
        checkbox.isChecked = j.value("isChecked", false);
        checkbox.groupName = j.value("groupName", std::string(""));
        checkbox.allowUncheck = j.value("allowUncheck", true);

        readVec4(j, "uncheckedColor", checkbox.uncheckedColor);
        readVec4(j, "checkedColor", checkbox.checkedColor);
        readVec4(j, "hoveredColor", checkbox.hoveredColor);
        readVec4(j, "disabledColor", checkbox.disabledColor);

        checkbox.uncheckedTextureRef = readAssetRef(j, "uncheckedTextureRef");
        checkbox.checkedTextureRef = readAssetRef(j, "checkedTextureRef");
        checkbox.hoveredTextureRef = readAssetRef(j, "hoveredTextureRef");
        checkbox.disabledTextureRef = readAssetRef(j, "disabledTextureRef");

        checkbox.colorTransitionDuration = j.value("colorTransitionDuration", 0.1f);
        checkbox.interactable = j.value("interactable", true);
        checkbox.labelToggle = j.value("labelToggle", false);

        checkbox.currentDisplayColor = checkbox.isChecked ? checkbox.checkedColor : checkbox.uncheckedColor;
    }

    // ---- Dropdown ----

    json SceneSerialization::serializeUIDropdown(const components::UIDropdownComponent& dropdown)
    {
        json j;

        json optionsArr = json::array();
        for (const auto& opt : dropdown.options)
        {
            json optJson;
            optJson["text"] = opt.text;
            if (opt.iconRef.isValid())
            {
                writeAssetRef(optJson, "iconRef", opt.iconRef);
            }
            optionsArr.push_back(optJson);
        }
        j["options"] = optionsArr;

        j["selectedIndex"] = dropdown.selectedIndex;
        j["placeholderText"] = dropdown.placeholderText;
        j["maxVisibleItems"] = dropdown.maxVisibleItems;
        j["interactable"] = dropdown.interactable;

        j["normalColor"] = writeVec4(dropdown.normalColor);
        j["hoveredColor"] = writeVec4(dropdown.hoveredColor);
        j["openColor"] = writeVec4(dropdown.openColor);
        j["disabledColor"] = writeVec4(dropdown.disabledColor);

        j["listBackgroundColor"] = writeVec4(dropdown.listBackgroundColor);
        j["itemNormalColor"] = writeVec4(dropdown.itemNormalColor);
        j["itemHoveredColor"] = writeVec4(dropdown.itemHoveredColor);

        if (dropdown.fontRef.isValid())
        {
            writeAssetRef(j, "fontRef", dropdown.fontRef);
        }
        j["fontSize"] = dropdown.fontSize;

        j["colorTransitionDuration"] = dropdown.colorTransitionDuration;

        return j;
    }

    void SceneSerialization::deserializeUIDropdown(const json& j, components::UIDropdownComponent& dropdown)
    {
        if (j.contains("options") && j["options"].is_array())
        {
            dropdown.options.clear();
            for (const auto& optJson : j["options"])
            {
                components::DropdownOption opt;
                opt.text = optJson.value("text", std::string(""));
                opt.iconRef = readAssetRef(optJson, "iconRef");
                dropdown.options.push_back(opt);
            }
        }

        dropdown.selectedIndex = j.value("selectedIndex", -1);
        dropdown.placeholderText = j.value("placeholderText", std::string("Select..."));
        dropdown.maxVisibleItems = j.value("maxVisibleItems", 5);
        dropdown.interactable = j.value("interactable", true);

        readVec4(j, "normalColor", dropdown.normalColor);
        readVec4(j, "hoveredColor", dropdown.hoveredColor);
        readVec4(j, "openColor", dropdown.openColor);
        readVec4(j, "disabledColor", dropdown.disabledColor);
        readVec4(j, "listBackgroundColor", dropdown.listBackgroundColor);
        readVec4(j, "itemNormalColor", dropdown.itemNormalColor);
        readVec4(j, "itemHoveredColor", dropdown.itemHoveredColor);

        dropdown.fontRef = readAssetRef(j, "fontRef");
        dropdown.fontSize = j.value("fontSize", 16.0f);
        dropdown.colorTransitionDuration = j.value("colorTransitionDuration", 0.1f);

        dropdown.currentDisplayColor = dropdown.normalColor;
    }

    // ---- Tabs ----

    json SceneSerialization::serializeUITabs(const components::UITabsComponent& tabs)
    {
        json j;

        std::string posStr;
        switch (tabs.tabBarPosition)
        {
        case components::TabBarPosition::Top: posStr = "Top"; break;
        case components::TabBarPosition::Bottom: posStr = "Bottom"; break;
        case components::TabBarPosition::Left: posStr = "Left"; break;
        case components::TabBarPosition::Right: posStr = "Right"; break;
        default: posStr = "Top"; break;
        }
        j["tabBarPosition"] = posStr;
        j["activeTabIndex"] = tabs.activeTabIndex;

        return j;
    }

    void SceneSerialization::deserializeUITabs(const json& j, components::UITabsComponent& tabs)
    {
        if (j.contains("tabBarPosition"))
        {
            std::string posStr = j["tabBarPosition"].get<std::string>();
            if (posStr == "Bottom") tabs.tabBarPosition = components::TabBarPosition::Bottom;
            else if (posStr == "Left") tabs.tabBarPosition = components::TabBarPosition::Left;
            else if (posStr == "Right") tabs.tabBarPosition = components::TabBarPosition::Right;
            else tabs.tabBarPosition = components::TabBarPosition::Top;
        }

        tabs.activeTabIndex = j.value("activeTabIndex", 0);

        tabs.previousTabIndex = -1;
    }

    // ---- Slider ----

    json SceneSerialization::serializeUISlider(const components::UISliderComponent& slider)
    {
        json j;
        j["minValue"] = slider.minValue;
        j["maxValue"] = slider.maxValue;
        j["value"] = slider.value;
        j["stepSize"] = slider.stepSize;

        switch (slider.orientation) {
            case components::UISliderOrientation::Vertical: j["orientation"] = "Vertical"; break;
            default: j["orientation"] = "Horizontal"; break;
        }

        j["clickTrackToSet"] = slider.clickTrackToSet;
        j["handleSizeRatio"] = slider.handleSizeRatio;

        j["handleNormalColor"] = writeVec4(slider.handleNormalColor);
        j["handleHoveredColor"] = writeVec4(slider.handleHoveredColor);
        j["handlePressedColor"] = writeVec4(slider.handlePressedColor);
        j["handleDisabledColor"] = writeVec4(slider.handleDisabledColor);

        if (slider.handleNormalTextureRef.isValid()) writeAssetRef(j, "handleNormalTextureRef", slider.handleNormalTextureRef);
        if (slider.handleHoveredTextureRef.isValid()) writeAssetRef(j, "handleHoveredTextureRef", slider.handleHoveredTextureRef);
        if (slider.handlePressedTextureRef.isValid()) writeAssetRef(j, "handlePressedTextureRef", slider.handlePressedTextureRef);
        if (slider.handleDisabledTextureRef.isValid()) writeAssetRef(j, "handleDisabledTextureRef", slider.handleDisabledTextureRef);

        j["fillColor"] = writeVec4(slider.fillColor);
        if (slider.fillTextureRef.isValid()) writeAssetRef(j, "fillTextureRef", slider.fillTextureRef);

        j["colorTransitionDuration"] = slider.colorTransitionDuration;
        j["interactable"] = slider.interactable;
        return j;
    }

    void SceneSerialization::deserializeUISlider(const json& j, components::UISliderComponent& slider)
    {
        slider.minValue = j.value("minValue", 0.0f);
        slider.maxValue = j.value("maxValue", 1.0f);
        slider.value = j.value("value", 0.5f);
        slider.stepSize = j.value("stepSize", 0.0f);

        std::string orientStr = j.value("orientation", std::string("Horizontal"));
        if (orientStr == "Vertical")
            slider.orientation = components::UISliderOrientation::Vertical;
        else
            slider.orientation = components::UISliderOrientation::Horizontal;

        slider.clickTrackToSet = j.value("clickTrackToSet", true);
        slider.handleSizeRatio = j.value("handleSizeRatio", 0.08f);

        readVec4(j, "handleNormalColor", slider.handleNormalColor);
        readVec4(j, "handleHoveredColor", slider.handleHoveredColor);
        readVec4(j, "handlePressedColor", slider.handlePressedColor);
        readVec4(j, "handleDisabledColor", slider.handleDisabledColor);

        slider.handleNormalTextureRef = readAssetRef(j, "handleNormalTextureRef");
        slider.handleHoveredTextureRef = readAssetRef(j, "handleHoveredTextureRef");
        slider.handlePressedTextureRef = readAssetRef(j, "handlePressedTextureRef");
        slider.handleDisabledTextureRef = readAssetRef(j, "handleDisabledTextureRef");

        readVec4(j, "fillColor", slider.fillColor);
        slider.fillTextureRef = readAssetRef(j, "fillTextureRef");

        slider.colorTransitionDuration = j.value("colorTransitionDuration", 0.1f);
        slider.interactable = j.value("interactable", true);

        slider.currentState = components::UISliderState::Normal;
        slider.currentHandleDisplayColor = slider.handleNormalColor;
        slider.isDragging = false;
        slider.dragStartValue = 0.0f;
    }

    // ---- ProgressBar ----

    json SceneSerialization::serializeUIProgressBar(const components::UIProgressBarComponent& pb)
    {
        json j;
        j["minValue"] = pb.minValue;
        j["maxValue"] = pb.maxValue;
        j["value"] = pb.value;

        switch (pb.orientation) {
            case components::UISliderOrientation::Vertical: j["orientation"] = "Vertical"; break;
            default: j["orientation"] = "Horizontal"; break;
        }

        j["invertDirection"] = pb.invertDirection;
        j["smoothInterpolation"] = pb.smoothInterpolation;
        j["interpolationSpeed"] = pb.interpolationSpeed;

        j["trackColor"] = writeVec4(pb.trackColor);
        if (pb.trackTextureRef.isValid()) writeAssetRef(j, "trackTextureRef", pb.trackTextureRef);

        j["fillColor"] = writeVec4(pb.fillColor);
        if (pb.fillTextureRef.isValid()) writeAssetRef(j, "fillTextureRef", pb.fillTextureRef);

        return j;
    }

    void SceneSerialization::deserializeUIProgressBar(const json& j, components::UIProgressBarComponent& pb)
    {
        pb.minValue = j.value("minValue", 0.0f);
        pb.maxValue = j.value("maxValue", 1.0f);
        pb.value = j.value("value", 0.0f);

        std::string orientStr = j.value("orientation", std::string("Horizontal"));
        if (orientStr == "Vertical")
            pb.orientation = components::UISliderOrientation::Vertical;
        else
            pb.orientation = components::UISliderOrientation::Horizontal;

        pb.invertDirection = j.value("invertDirection", false);
        pb.smoothInterpolation = j.value("smoothInterpolation", false);
        pb.interpolationSpeed = j.value("interpolationSpeed", 5.0f);

        readVec4(j, "trackColor", pb.trackColor);
        pb.trackTextureRef = readAssetRef(j, "trackTextureRef");

        readVec4(j, "fillColor", pb.fillColor);
        pb.fillTextureRef = readAssetRef(j, "fillTextureRef");

        pb.displayValue = pb.value;
        pb.completedFired = (pb.value >= pb.maxValue);
    }

} // namespace serialization
