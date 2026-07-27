#include "UILabelDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UIEvents.hpp"
#include "asset/AssetRef.hpp"
#include "DrawerHelpers.hpp"
#include "FontSlotWidget.hpp"
#include <imgui.h>
#include <cstring>

namespace windows::details
{
    bool UILabelDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUILabelComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasLabel = dispatcher.query(hasQuery);

        if (!hasLabel)
        {
            return false;
        }

        events::ui::GetUILabelDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("UILabelComponent");

        bool removeLabel = false;
        bool isOpen = drawHeader(removeLabel);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UILabelData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Text label with font, alignment, and overflow settings");
            ImGui::Spacing();

            changed |= drawText(data);
            ImGui::Spacing();
            changed |= drawFontPath(data);
            ImGui::Spacing();
            changed |= drawFontSize(data);
            changed |= drawFontStyle(data);
            ImGui::Spacing();
            changed |= drawColor(data);
            ImGui::Spacing();
            changed |= drawAlignment(data);
            ImGui::Spacing();
            changed |= drawOverflow(data);
            ImGui::Spacing();
            changed |= drawSpacing(data);
            ImGui::Spacing();
            changed |= drawTextEffects(data.effects, "UILabel");

            if (changed)
            {
                events::ui::SetUILabelDataCommand cmd;
                cmd.entity = handle;
                cmd.labelData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeLabel)
        {
            events::ui::RemoveUILabelComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UILabelDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UILabelHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Label");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUILabel", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool UILabelDrawer::drawText(services::UILabelData& data)
    {
        bool changed = false;

        char buffer[4096];
        std::strncpy(buffer, data.text.c_str(), sizeof(buffer));
        buffer[sizeof(buffer) - 1] = '\0';

        ImGui::Text("Text");
        if (ImGui::InputTextMultiline("##UILabelText", buffer, sizeof(buffer), ImVec2(-1, 80)))
        {
            data.text = buffer;
            changed = true;
        }

        return changed;
    }

    bool UILabelDrawer::drawFontPath(services::UILabelData& data)
    {
        return drawFontSlot(data.fontRef, "UILabel");
    }

    bool UILabelDrawer::drawFontSize(services::UILabelData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat("Font Size##UILabel", &data.fontSize, 0.5f, 1.0f, 200.0f, "%.1f"))
        {
            changed = true;
        }

        return changed;
    }

    bool UILabelDrawer::drawFontStyle(services::UILabelData& data)
    {
        bool changed = false;

        const char* styles[] = {"Normal", "Bold", "Italic", "BoldItalic"};
        int style = static_cast<int>(data.fontStyle);

        if (ImGui::Combo("Font Style##UILabel", &style, styles, 4))
        {
            data.fontStyle = static_cast<uint8_t>(style);
            changed = true;
        }

        return changed;
    }

    bool UILabelDrawer::drawColor(services::UILabelData& data)
    {
        bool changed = false;

        if (ColorEditRow("Color##UILabel", &data.color.x))
        {
            changed = true;
        }

        return changed;
    }

    bool UILabelDrawer::drawAlignment(services::UILabelData& data)
    {
        bool changed = false;

        const char* hAlignments[] = {"Left", "Center", "Right"};
        int hAlign = static_cast<int>(data.horizontalAlignment);

        if (ImGui::Combo("Horizontal##UILabel", &hAlign, hAlignments, 3))
        {
            data.horizontalAlignment = static_cast<uint8_t>(hAlign);
            changed = true;
        }

        const char* vAlignments[] = {"Top", "Middle", "Bottom"};
        int vAlign = static_cast<int>(data.verticalAlignment);

        if (ImGui::Combo("Vertical##UILabel", &vAlign, vAlignments, 3))
        {
            data.verticalAlignment = static_cast<uint8_t>(vAlign);
            changed = true;
        }

        return changed;
    }

    bool UILabelDrawer::drawOverflow(services::UILabelData& data)
    {
        bool changed = false;

        const char* overflows[] = {"Overflow", "Clip", "Ellipsis"};
        int overflow = static_cast<int>(data.overflow);

        if (ImGui::Combo("Overflow##UILabel", &overflow, overflows, 3))
        {
            data.overflow = static_cast<uint8_t>(overflow);
            changed = true;
        }

        return changed;
    }

    bool UILabelDrawer::drawSpacing(services::UILabelData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat("Line Spacing##UILabel", &data.lineSpacing, 0.01f, 0.1f, 5.0f, "%.2f"))
        {
            changed = true;
        }

        if (ImGui::DragFloat("Letter Spacing##UILabel", &data.letterSpacing, 0.1f, -10.0f, 50.0f, "%.1f"))
        {
            changed = true;
        }

        if (ImGui::Checkbox("Word Wrap##UILabel", &data.wordWrap))
        {
            changed = true;
        }

        if (ImGui::Checkbox("Rich Text##UILabel", &data.richText))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Parse [b], [i], [color=#RRGGBB] markup. [[ escapes a literal [.");
        }

        return changed;
    }
}
