#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"

namespace {
    using json = nlohmann::json;

    void readVec4(const json& j, const std::string& key, glm::vec4& out) {
        if (j.contains(key) && j[key].is_array() && j[key].size() >= 4) {
            out = glm::vec4(j[key][0].get<float>(), j[key][1].get<float>(),
                           j[key][2].get<float>(), j[key][3].get<float>());
        }
    }

    json writeVec4(const glm::vec4& v) {
        return json::array({v.x, v.y, v.z, v.w});
    }
}

namespace serialization {

    // ---- Label ----

    json SceneSerialization::serializeUILabel(const components::UILabelComponent& label)
    {
        json j;
        if (!label.text.empty())
        {
            j["text"] = label.text;
        }
        if (!label.fontPath.empty())
        {
            j["fontPath"] = label.fontPath;
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
        return j;
    }

    void SceneSerialization::deserializeUILabel(const json& j, components::UILabelComponent& label)
    {
        label.text = j.value("text", std::string("Label"));
        label.fontPath = j.value("fontPath", std::string(""));
        label.fontSize = j.value("fontSize", 16.0f);
        label.fontStyle = stringToFontStyle(j.value("fontStyle", "normal"));
        readVec4(j, "color", label.color);
        label.horizontalAlignment = stringToHorizontalAlignment(j.value("horizontalAlignment", "left"));
        label.verticalAlignment = stringToVerticalAlignment(j.value("verticalAlignment", "top"));
        label.overflow = stringToTextOverflow(j.value("overflow", "overflow"));
        label.wordWrap = j.value("wordWrap", true);
        label.lineSpacing = j.value("lineSpacing", 1.0f);
        label.letterSpacing = j.value("letterSpacing", 0.0f);
    }

    // ---- Button ----

    json SceneSerialization::serializeUIButton(const components::UIButtonComponent& button)
    {
        json j;

        j["normalColor"] = writeVec4(button.normalColor);
        j["hoveredColor"] = writeVec4(button.hoveredColor);
        j["pressedColor"] = writeVec4(button.pressedColor);
        j["disabledColor"] = writeVec4(button.disabledColor);

        if (!button.normalTexture.empty())
        {
            j["normalTexture"] = button.normalTexture;
        }
        if (!button.hoverTexture.empty())
        {
            j["hoverTexture"] = button.hoverTexture;
        }
        if (!button.pressedTexture.empty())
        {
            j["pressedTexture"] = button.pressedTexture;
        }
        if (!button.disabledTexture.empty())
        {
            j["disabledTexture"] = button.disabledTexture;
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

        button.normalTexture = j.value("normalTexture", std::string(""));
        button.hoverTexture = j.value("hoverTexture", std::string(""));
        button.pressedTexture = j.value("pressedTexture", std::string(""));
        button.disabledTexture = j.value("disabledTexture", std::string(""));

        button.colorTransitionDuration = j.value("colorTransitionDuration", 0.1f);
        button.interactable = j.value("interactable", true);
    }

    // ---- TextInput ----

    json SceneSerialization::serializeUITextInput(const components::UITextInputComponent& textInput)
    {
        json j;

        j["text"] = textInput.text;
        j["placeholderText"] = textInput.placeholderText;

        if (!textInput.fontPath.empty())
        {
            j["fontPath"] = textInput.fontPath;
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
        textInput.fontPath = j.value("fontPath", std::string(""));
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

        if (!checkbox.uncheckedTexture.empty())
        {
            j["uncheckedTexture"] = checkbox.uncheckedTexture;
        }
        if (!checkbox.checkedTexture.empty())
        {
            j["checkedTexture"] = checkbox.checkedTexture;
        }
        if (!checkbox.hoveredTexture.empty())
        {
            j["hoveredTexture"] = checkbox.hoveredTexture;
        }
        if (!checkbox.disabledTexture.empty())
        {
            j["disabledTexture"] = checkbox.disabledTexture;
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

        checkbox.uncheckedTexture = j.value("uncheckedTexture", std::string(""));
        checkbox.checkedTexture = j.value("checkedTexture", std::string(""));
        checkbox.hoveredTexture = j.value("hoveredTexture", std::string(""));
        checkbox.disabledTexture = j.value("disabledTexture", std::string(""));

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
            if (!opt.iconPath.empty())
            {
                optJson["iconPath"] = opt.iconPath;
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

        if (!dropdown.fontPath.empty())
        {
            j["fontPath"] = dropdown.fontPath;
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
                opt.iconPath = optJson.value("iconPath", std::string(""));
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

        dropdown.fontPath = j.value("fontPath", std::string(""));
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

        if (!slider.handleNormalTexture.empty()) j["handleNormalTexture"] = slider.handleNormalTexture;
        if (!slider.handleHoveredTexture.empty()) j["handleHoveredTexture"] = slider.handleHoveredTexture;
        if (!slider.handlePressedTexture.empty()) j["handlePressedTexture"] = slider.handlePressedTexture;
        if (!slider.handleDisabledTexture.empty()) j["handleDisabledTexture"] = slider.handleDisabledTexture;

        j["fillColor"] = writeVec4(slider.fillColor);
        if (!slider.fillTexture.empty()) j["fillTexture"] = slider.fillTexture;

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

        slider.handleNormalTexture = j.value("handleNormalTexture", std::string(""));
        slider.handleHoveredTexture = j.value("handleHoveredTexture", std::string(""));
        slider.handlePressedTexture = j.value("handlePressedTexture", std::string(""));
        slider.handleDisabledTexture = j.value("handleDisabledTexture", std::string(""));

        readVec4(j, "fillColor", slider.fillColor);
        slider.fillTexture = j.value("fillTexture", std::string(""));

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
        if (!pb.trackTexture.empty()) j["trackTexture"] = pb.trackTexture;

        j["fillColor"] = writeVec4(pb.fillColor);
        if (!pb.fillTexture.empty()) j["fillTexture"] = pb.fillTexture;

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
        pb.trackTexture = j.value("trackTexture", std::string(""));

        readVec4(j, "fillColor", pb.fillColor);
        pb.fillTexture = j.value("fillTexture", std::string(""));

        pb.displayValue = pb.value;
        pb.completedFired = (pb.value >= pb.maxValue);
    }

    // ---- UI Animation helpers ----

    static std::string easingFunctionToString(components::UIEasingFunction easing)
    {
        switch (easing)
        {
        case components::UIEasingFunction::Linear: return "Linear";
        case components::UIEasingFunction::EaseIn: return "EaseIn";
        case components::UIEasingFunction::EaseOut: return "EaseOut";
        case components::UIEasingFunction::EaseInOut: return "EaseInOut";
        case components::UIEasingFunction::Bounce: return "Bounce";
        case components::UIEasingFunction::Elastic: return "Elastic";
        default: return "Linear";
        }
    }

    static components::UIEasingFunction stringToEasingFunction(const std::string& str)
    {
        if (str == "EaseIn") return components::UIEasingFunction::EaseIn;
        if (str == "EaseOut") return components::UIEasingFunction::EaseOut;
        if (str == "EaseInOut") return components::UIEasingFunction::EaseInOut;
        if (str == "Bounce") return components::UIEasingFunction::Bounce;
        if (str == "Elastic") return components::UIEasingFunction::Elastic;
        return components::UIEasingFunction::Linear;
    }

    static std::string loopModeToString(components::UIAnimationLoopMode mode)
    {
        switch (mode)
        {
        case components::UIAnimationLoopMode::Once: return "Once";
        case components::UIAnimationLoopMode::Loop: return "Loop";
        case components::UIAnimationLoopMode::PingPong: return "PingPong";
        default: return "Once";
        }
    }

    static components::UIAnimationLoopMode stringToLoopMode(const std::string& str)
    {
        if (str == "Loop") return components::UIAnimationLoopMode::Loop;
        if (str == "PingPong") return components::UIAnimationLoopMode::PingPong;
        return components::UIAnimationLoopMode::Once;
    }

    static std::string tweenPropertyToString(components::UITweenProperty prop)
    {
        switch (prop)
        {
        case components::UITweenProperty::Opacity: return "Opacity";
        case components::UITweenProperty::PositionX: return "PositionX";
        case components::UITweenProperty::PositionY: return "PositionY";
        case components::UITweenProperty::ScaleX: return "ScaleX";
        case components::UITweenProperty::ScaleY: return "ScaleY";
        case components::UITweenProperty::Rotation: return "Rotation";
        case components::UITweenProperty::ColorR: return "ColorR";
        case components::UITweenProperty::ColorG: return "ColorG";
        case components::UITweenProperty::ColorB: return "ColorB";
        case components::UITweenProperty::ColorA: return "ColorA";
        default: return "Opacity";
        }
    }

    static components::UITweenProperty stringToTweenProperty(const std::string& str)
    {
        if (str == "PositionX") return components::UITweenProperty::PositionX;
        if (str == "PositionY") return components::UITweenProperty::PositionY;
        if (str == "ScaleX") return components::UITweenProperty::ScaleX;
        if (str == "ScaleY") return components::UITweenProperty::ScaleY;
        if (str == "Rotation") return components::UITweenProperty::Rotation;
        if (str == "ColorR") return components::UITweenProperty::ColorR;
        if (str == "ColorG") return components::UITweenProperty::ColorG;
        if (str == "ColorB") return components::UITweenProperty::ColorB;
        if (str == "ColorA") return components::UITweenProperty::ColorA;
        return components::UITweenProperty::Opacity;
    }

    static std::string animNodeTypeToString(components::UIAnimationNodeType type)
    {
        switch (type)
        {
        case components::UIAnimationNodeType::Clip: return "Clip";
        case components::UIAnimationNodeType::Parallel: return "Parallel";
        case components::UIAnimationNodeType::Sequence: return "Sequence";
        default: return "Clip";
        }
    }

    static components::UIAnimationNodeType stringToAnimNodeType(const std::string& str)
    {
        if (str == "Parallel") return components::UIAnimationNodeType::Parallel;
        if (str == "Sequence") return components::UIAnimationNodeType::Sequence;
        return components::UIAnimationNodeType::Clip;
    }

    static json serializeUIAnimationNode(const components::UIAnimationNode& node)
    {
        json j;
        j["type"] = animNodeTypeToString(node.type);
        j["loopMode"] = loopModeToString(node.loopMode);

        if (node.type == components::UIAnimationNodeType::Clip)
        {
            json clipJson;
            clipJson["property"] = tweenPropertyToString(node.clip.property);
            clipJson["startValue"] = node.clip.startValue;
            clipJson["endValue"] = node.clip.endValue;
            clipJson["duration"] = node.clip.duration;
            clipJson["delay"] = node.clip.delay;
            clipJson["easing"] = easingFunctionToString(node.clip.easing);
            clipJson["loopMode"] = loopModeToString(node.clip.loopMode);
            j["clip"] = clipJson;
        }

        if (!node.children.empty())
        {
            json childrenArr = json::array();
            for (const auto& child : node.children)
            {
                childrenArr.push_back(serializeUIAnimationNode(child));
            }
            j["children"] = childrenArr;
        }

        return j;
    }

    static void deserializeUIAnimationNode(const json& j, components::UIAnimationNode& node)
    {
        node.type = stringToAnimNodeType(j.value("type", std::string("Clip")));
        node.loopMode = stringToLoopMode(j.value("loopMode", std::string("Once")));

        if (j.contains("clip") && j["clip"].is_object())
        {
            const auto& clipJson = j["clip"];
            node.clip.property = stringToTweenProperty(clipJson.value("property", std::string("Opacity")));
            node.clip.startValue = clipJson.value("startValue", 0.0f);
            node.clip.endValue = clipJson.value("endValue", 1.0f);
            node.clip.duration = clipJson.value("duration", 1.0f);
            node.clip.delay = clipJson.value("delay", 0.0f);
            node.clip.easing = stringToEasingFunction(clipJson.value("easing", std::string("Linear")));
            node.clip.loopMode = stringToLoopMode(clipJson.value("loopMode", std::string("Once")));
        }

        if (j.contains("children") && j["children"].is_array())
        {
            node.children.clear();
            for (const auto& childJson : j["children"])
            {
                components::UIAnimationNode child;
                deserializeUIAnimationNode(childJson, child);
                node.children.push_back(std::move(child));
            }
        }
    }

    // ---- UI Animation ----

    json SceneSerialization::serializeUIAnimation(const components::UIAnimationComponent& anim)
    {
        json j;
        j["rootNode"] = serializeUIAnimationNode(anim.rootNode);
        j["autoPlay"] = anim.autoPlay;
        return j;
    }

    void SceneSerialization::deserializeUIAnimation(const json& j, components::UIAnimationComponent& anim)
    {
        if (j.contains("rootNode") && j["rootNode"].is_object())
        {
            deserializeUIAnimationNode(j["rootNode"], anim.rootNode);
        }
        anim.autoPlay = j.value("autoPlay", false);

        // Reset runtime state
        anim.isPlaying = false;
        anim.isPaused = false;
        anim.elapsedTime = 0.0f;
        anim.startedFired = false;
        anim.completedFired = false;
    }

    // ---- UI Mask ----

    json SceneSerialization::serializeUIMask(const components::UIMaskComponent& mask)
    {
        json j;
        j["maskMode"] = static_cast<int>(mask.maskMode);
        j["maskTexturePath"] = mask.maskTexturePath;
        j["alphaThreshold"] = mask.alphaThreshold;
        j["showMaskGraphic"] = mask.showMaskGraphic;
        return j;
    }

    void SceneSerialization::deserializeUIMask(const json& j, components::UIMaskComponent& mask)
    {
        if (j.contains("maskMode"))
            mask.maskMode = static_cast<components::UIMaskMode>(j["maskMode"].get<int>());
        if (j.contains("maskTexturePath"))
            mask.maskTexturePath = j["maskTexturePath"].get<std::string>();
        if (j.contains("alphaThreshold"))
            mask.alphaThreshold = j["alphaThreshold"].get<float>();
        if (j.contains("showMaskGraphic"))
            mask.showMaskGraphic = j["showMaskGraphic"].get<bool>();
    }

} // namespace serialization
