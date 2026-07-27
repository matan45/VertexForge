#include "TextDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "asset/AssetRef.hpp"
#include "DrawerHelpers.hpp"
#include "FontSlotWidget.hpp"
#include <imgui.h>
#include <cstring>

namespace windows::details
{
    bool TextDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasTextComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasText = dispatcher.query(hasQuery);

        if (!hasText)
        {
            return false;
        }

        events::scene::GetTextDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("TextComponent");

        bool removeText = false;
        bool isOpen = drawHeader(removeText);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::TextData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("SDF text rendering");
            ImGui::Spacing();

            changed |= drawFontPath(data);
            ImGui::Spacing();
            changed |= drawTextInput(data);
            ImGui::Spacing();
            changed |= drawFontSize(data);
            ImGui::Spacing();
            changed |= drawFontStyle(data);
            ImGui::Spacing();
            changed |= drawColor(data);
            ImGui::Spacing();
            changed |= drawLineSpacing(data);
            ImGui::Spacing();
            changed |= drawLetterSpacing(data);
            ImGui::Spacing();
            changed |= drawMaxWidth(data);
            ImGui::Spacing();
            changed |= drawTextEffects(data.effects, "Text");

            if (changed)
            {
                events::scene::SetTextDataCommand cmd;
                cmd.entity = handle;
                cmd.textData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeText)
        {
            events::scene::RemoveTextComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool TextDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##TextHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Text");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveText", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool TextDrawer::drawFontPath(services::TextData& data)
    {
        return drawFontSlot(data.fontRef, "Text");
    }

    bool TextDrawer::drawTextInput(services::TextData& data)
    {
        bool changed = false;

        std::strncpy(textBuffer, data.text.c_str(), sizeof(textBuffer) - 1);
        textBuffer[sizeof(textBuffer) - 1] = '\0';

        if (ImGui::InputTextMultiline("##TextContent", textBuffer, sizeof(textBuffer),
                                       ImVec2(-1, 80)))
        {
            data.text = textBuffer;
            changed = true;
        }

        return changed;
    }

    bool TextDrawer::drawFontSize(services::TextData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat("Font Size##Text", &data.fontSize, 0.5f, 1.0f, 256.0f, "%.1f"))
        {
            changed = true;
        }

        return changed;
    }

    bool TextDrawer::drawFontStyle(services::TextData& data)
    {
        bool changed = false;

        const char* styles[] = {"Normal", "Bold", "Italic", "BoldItalic"};
        int style = static_cast<int>(data.fontStyle);

        if (ImGui::Combo("Font Style##Text", &style, styles, 4))
        {
            data.fontStyle = static_cast<uint8_t>(style);
            changed = true;
        }

        return changed;
    }

    bool TextDrawer::drawColor(services::TextData& data)
    {
        bool changed = false;

        if (ImGui::ColorEdit4("Color##Text", &data.color.x))
        {
            changed = true;
        }

        return changed;
    }

    bool TextDrawer::drawLineSpacing(services::TextData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat("Line Spacing##Text", &data.lineSpacing, 0.01f, 0.5f, 3.0f, "%.2f"))
        {
            changed = true;
        }

        return changed;
    }

    bool TextDrawer::drawLetterSpacing(services::TextData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat("Letter Spacing##Text", &data.letterSpacing, 0.1f, -10.0f, 50.0f, "%.1f"))
        {
            changed = true;
        }

        return changed;
    }

    bool TextDrawer::drawMaxWidth(services::TextData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat("Max Width##Text", &data.maxWidth, 1.0f, 0.0f, 10000.0f, "%.0f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("0 = no word wrap");
        }

        return changed;
    }
}
