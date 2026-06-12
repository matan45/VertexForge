#include "SceneSerialization.hpp"
#include "AssetRefSerializationHelper.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"

namespace serialization {

    // ---- Canvas ----

    json SceneSerialization::serializeUICanvas(const components::UICanvasComponent& canvas)
    {
        json j;
        j["referenceWidth"] = canvas.referenceWidth;
        j["referenceHeight"] = canvas.referenceHeight;
        j["scaleMode"] = uiScaleModeToString(canvas.scaleMode);
        j["pixelsPerUnit"] = canvas.pixelsPerUnit;
        j["sortOrder"] = canvas.sortOrder;
        if (canvas.themeRef.isValid())
        {
            writeAssetRef(j, "themeRef", canvas.themeRef);
        }
        return j;
    }

    void SceneSerialization::deserializeUICanvas(const json& j, components::UICanvasComponent& canvas)
    {
        canvas.referenceWidth = j.value("referenceWidth", 1920.0f);
        canvas.referenceHeight = j.value("referenceHeight", 1080.0f);
        canvas.scaleMode = stringToUIScaleMode(j.value("scaleMode", "scaleWithScreenSize"));
        canvas.pixelsPerUnit = j.value("pixelsPerUnit", 100.0f);
        canvas.sortOrder = j.value("sortOrder", 0);
        canvas.themeRef = readAssetRef(j, "themeRef", "");
    }

    // ---- Style ----

    json SceneSerialization::serializeUIStyle(const components::UIStyleComponent& style)
    {
        json j;
        j["styleKey"] = style.styleKey;
        return j;
    }

    void SceneSerialization::deserializeUIStyle(const json& j, components::UIStyleComponent& style)
    {
        style.styleKey = j.value("styleKey", std::string());
    }

    // ---- Rect ----

    json SceneSerialization::serializeUIRect(const components::UIRectComponent& rect)
    {
        json j;
        j["anchorMin"] = writeVec2(rect.anchorMin);
        j["anchorMax"] = writeVec2(rect.anchorMax);
        j["pivot"] = writeVec2(rect.pivot);
        j["sizeDelta"] = writeVec2(rect.sizeDelta);
        j["anchoredPosition"] = writeVec2(rect.anchoredPosition);
        j["blocksRaycast"] = rect.blocksRaycast;
        return j;
    }

    void SceneSerialization::deserializeUIRect(const json& j, components::UIRectComponent& rect)
    {
        readVec2(j, "anchorMin", rect.anchorMin);
        readVec2(j, "anchorMax", rect.anchorMax);
        readVec2(j, "pivot", rect.pivot);
        readVec2(j, "sizeDelta", rect.sizeDelta);
        readVec2(j, "anchoredPosition", rect.anchoredPosition);
        rect.blocksRaycast = j.value("blocksRaycast", true);
    }

    // ---- Image ----

    json SceneSerialization::serializeUIImage(const components::UIImageComponent& image)
    {
        json j;
        if (image.textureRef.isValid())
        {
            writeAssetRef(j, "textureRef", image.textureRef);
        }
        j["colorTint"] = writeVec4(image.colorTint);
        if (!image.renderTextureSourceName.empty())
        {
            j["renderTextureSourceName"] = image.renderTextureSourceName;
        }
        if (image.imageType != components::UIImageType::Simple)
        {
            j["imageType"] = static_cast<int>(image.imageType);
        }
        if (image.border.x != 0.0f || image.border.y != 0.0f || image.border.z != 0.0f || image.border.w != 0.0f)
        {
            j["border"] = writeVec4(image.border);
        }
        if (image.sourceWidth > 0 && image.sourceHeight > 0)
        {
            j["sourceWidth"] = image.sourceWidth;
            j["sourceHeight"] = image.sourceHeight;
        }
        return j;
    }

    void SceneSerialization::deserializeUIImage(const json& j, components::UIImageComponent& image)
    {
        image.textureRef = readAssetRef(j, "textureRef", "texturePath");
        readVec4(j, "colorTint", image.colorTint);
        image.renderTextureSourceName = j.value("renderTextureSourceName", std::string(""));
        image.renderTextureSource = entt::null; // Resolved post-load
        image.imageType = static_cast<components::UIImageType>(j.value("imageType", 0));
        readVec4(j, "border", image.border);
        image.sourceWidth = j.value("sourceWidth", 0u);
        image.sourceHeight = j.value("sourceHeight", 0u);
    }

    // ---- ScaleMode enum ----

    std::string SceneSerialization::uiScaleModeToString(components::UIScaleMode mode)
    {
        switch (mode)
        {
        case components::UIScaleMode::ConstantPixelSize: return "constantPixelSize";
        case components::UIScaleMode::ScaleWithScreenSize: return "scaleWithScreenSize";
        default: return "scaleWithScreenSize";
        }
    }

    components::UIScaleMode SceneSerialization::stringToUIScaleMode(const std::string& str)
    {
        if (str == "constantPixelSize") return components::UIScaleMode::ConstantPixelSize;
        if (str == "scaleWithScreenSize") return components::UIScaleMode::ScaleWithScreenSize;
        return components::UIScaleMode::ScaleWithScreenSize;
    }

    // ---- Scroll ----

    json SceneSerialization::serializeUIScroll(const components::UIScrollComponent& scroll)
    {
        json j;
        j["horizontalScrollEnabled"] = scroll.horizontalScrollEnabled;
        j["verticalScrollEnabled"] = scroll.verticalScrollEnabled;
        j["horizontalScrollbarVisibility"] = scrollbarVisibilityToString(scroll.horizontalScrollbarVisibility);
        j["verticalScrollbarVisibility"] = scrollbarVisibilityToString(scroll.verticalScrollbarVisibility);
        j["scrollSensitivity"] = scroll.scrollSensitivity;
        return j;
    }

    void SceneSerialization::deserializeUIScroll(const json& j, components::UIScrollComponent& scroll)
    {
        scroll.horizontalScrollEnabled = j.value("horizontalScrollEnabled", false);
        scroll.verticalScrollEnabled = j.value("verticalScrollEnabled", true);
        scroll.horizontalScrollbarVisibility = stringToScrollbarVisibility(j.value("horizontalScrollbarVisibility", "auto"));
        scroll.verticalScrollbarVisibility = stringToScrollbarVisibility(j.value("verticalScrollbarVisibility", "auto"));
        scroll.scrollSensitivity = j.value("scrollSensitivity", 1.0f);
    }

    // ---- ScrollbarVisibility enum ----

    std::string SceneSerialization::scrollbarVisibilityToString(components::ScrollbarVisibility visibility)
    {
        switch (visibility)
        {
        case components::ScrollbarVisibility::Auto: return "auto";
        case components::ScrollbarVisibility::AlwaysVisible: return "alwaysVisible";
        case components::ScrollbarVisibility::Hidden: return "hidden";
        default: return "auto";
        }
    }

    components::ScrollbarVisibility SceneSerialization::stringToScrollbarVisibility(const std::string& str)
    {
        if (str == "alwaysVisible") return components::ScrollbarVisibility::AlwaysVisible;
        if (str == "hidden") return components::ScrollbarVisibility::Hidden;
        return components::ScrollbarVisibility::Auto;
    }

    // ---- LayoutGroup ----

    json SceneSerialization::serializeUILayoutGroup(const components::UILayoutGroupComponent& layoutGroup)
    {
        json j;
        j["direction"] = layoutDirectionToString(layoutGroup.direction);
        j["spacing"] = layoutGroup.spacing;
        j["padding"] = writeVec4(layoutGroup.padding);
        j["childAlignment"] = childAlignmentToString(layoutGroup.childAlignment);
        j["constraintCount"] = layoutGroup.constraintCount;
        return j;
    }

    void SceneSerialization::deserializeUILayoutGroup(const json& j, components::UILayoutGroupComponent& layoutGroup)
    {
        layoutGroup.direction = stringToLayoutDirection(j.value("direction", "vertical"));
        layoutGroup.spacing = j.value("spacing", 0.0f);
        readVec4(j, "padding", layoutGroup.padding);
        layoutGroup.childAlignment = stringToChildAlignment(j.value("childAlignment", "start"));
        layoutGroup.constraintCount = j.value("constraintCount", 2);
    }

    // ---- LayoutDirection enum ----

    std::string SceneSerialization::layoutDirectionToString(components::LayoutDirection direction)
    {
        switch (direction)
        {
        case components::LayoutDirection::Vertical: return "vertical";
        case components::LayoutDirection::Horizontal: return "horizontal";
        case components::LayoutDirection::Grid: return "grid";
        default: return "vertical";
        }
    }

    components::LayoutDirection SceneSerialization::stringToLayoutDirection(const std::string& str)
    {
        if (str == "horizontal") return components::LayoutDirection::Horizontal;
        if (str == "grid") return components::LayoutDirection::Grid;
        return components::LayoutDirection::Vertical;
    }

    // ---- ChildAlignment enum ----

    std::string SceneSerialization::childAlignmentToString(components::ChildAlignment alignment)
    {
        switch (alignment)
        {
        case components::ChildAlignment::Start: return "start";
        case components::ChildAlignment::Center: return "center";
        case components::ChildAlignment::End: return "end";
        default: return "start";
        }
    }

    components::ChildAlignment SceneSerialization::stringToChildAlignment(const std::string& str)
    {
        if (str == "center") return components::ChildAlignment::Center;
        if (str == "end") return components::ChildAlignment::End;
        return components::ChildAlignment::Start;
    }

    // ---- Text enum helpers ----

    std::string SceneSerialization::horizontalAlignmentToString(components::HorizontalAlignment alignment)
    {
        switch (alignment)
        {
        case components::HorizontalAlignment::Left: return "left";
        case components::HorizontalAlignment::Center: return "center";
        case components::HorizontalAlignment::Right: return "right";
        default: return "left";
        }
    }

    components::HorizontalAlignment SceneSerialization::stringToHorizontalAlignment(const std::string& str)
    {
        if (str == "center") return components::HorizontalAlignment::Center;
        if (str == "right") return components::HorizontalAlignment::Right;
        return components::HorizontalAlignment::Left;
    }

    std::string SceneSerialization::verticalAlignmentToString(components::VerticalAlignment alignment)
    {
        switch (alignment)
        {
        case components::VerticalAlignment::Top: return "top";
        case components::VerticalAlignment::Middle: return "middle";
        case components::VerticalAlignment::Bottom: return "bottom";
        default: return "top";
        }
    }

    components::VerticalAlignment SceneSerialization::stringToVerticalAlignment(const std::string& str)
    {
        if (str == "middle") return components::VerticalAlignment::Middle;
        if (str == "bottom") return components::VerticalAlignment::Bottom;
        return components::VerticalAlignment::Top;
    }

    std::string SceneSerialization::textOverflowToString(components::TextOverflow overflow)
    {
        switch (overflow)
        {
        case components::TextOverflow::Overflow: return "overflow";
        case components::TextOverflow::Clip: return "clip";
        case components::TextOverflow::Ellipsis: return "ellipsis";
        default: return "overflow";
        }
    }

    components::TextOverflow SceneSerialization::stringToTextOverflow(const std::string& str)
    {
        if (str == "clip") return components::TextOverflow::Clip;
        if (str == "ellipsis") return components::TextOverflow::Ellipsis;
        return components::TextOverflow::Overflow;
    }

    std::string SceneSerialization::fontStyleToString(components::FontStyle style)
    {
        switch (style)
        {
        case components::FontStyle::Normal: return "normal";
        case components::FontStyle::Bold: return "bold";
        case components::FontStyle::Italic: return "italic";
        case components::FontStyle::BoldItalic: return "boldItalic";
        default: return "normal";
        }
    }

    components::FontStyle SceneSerialization::stringToFontStyle(const std::string& str)
    {
        if (str == "bold") return components::FontStyle::Bold;
        if (str == "italic") return components::FontStyle::Italic;
        if (str == "boldItalic") return components::FontStyle::BoldItalic;
        return components::FontStyle::Normal;
    }

} // namespace serialization
