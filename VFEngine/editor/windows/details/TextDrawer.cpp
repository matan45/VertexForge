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
            changed |= drawAlignment(data);
            ImGui::Spacing();
            changed |= drawOverflow(data);
            ImGui::Spacing();
            changed |= drawLineSpacing(data);
            ImGui::Spacing();
            changed |= drawLetterSpacing(data);
            ImGui::Spacing();
            changed |= drawTextBox(data);
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

    // VK-1637. In all three combos below the item index IS the enum ordinal - the arrays
    // are ordered to match components::HorizontalAlignment / VerticalAlignment /
    // TextOverflow. Same coupling as UILabelDrawer; reordering an enum silently corrupts
    // both drawers.
    bool TextDrawer::drawAlignment(services::TextData& data)
    {
        bool changed = false;

        const char* hAlignments[] = {"Left", "Center", "Right"};
        int hAlign = static_cast<int>(data.horizontalAlignment);

        if (ImGui::Combo("Horizontal##Text", &hAlign, hAlignments, 3))
        {
            data.horizontalAlignment = static_cast<uint8_t>(hAlign);
            changed = true;
        }

        const char* vAlignments[] = {"Top", "Middle", "Bottom"};
        int vAlign = static_cast<int>(data.verticalAlignment);

        if (ImGui::Combo("Vertical##Text", &vAlign, vAlignments, 3))
        {
            data.verticalAlignment = static_cast<uint8_t>(vAlign);
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Needs a Rect Height to align inside.\n"
                              "With Rect Height 0 the text is always top-aligned.");
        }

        return changed;
    }

    bool TextDrawer::drawOverflow(services::TextData& data)
    {
        bool changed = false;

        const char* overflows[] = {"Overflow", "Clip", "Ellipsis"};
        int overflow = static_cast<int>(data.overflow);

        if (ImGui::Combo("Overflow##Text", &overflow, overflows, 3))
        {
            data.overflow = static_cast<uint8_t>(overflow);
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Ellipsis truncates each line to Max Width and appends an\n"
                              "ellipsis glyph.\n"
                              "Clip is not supported for world text (the 3D text pipeline\n"
                              "has no scissor) and renders as Overflow.");
        }

        return changed;
    }

    bool TextDrawer::drawTextBox(services::TextData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat("Max Width##Text", &data.maxWidth, 1.0f, 0.0f, 10000.0f, "%.0f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            // Deliberately not "0 = no alignment": with no box, horizontal alignment still
            // works - it aligns each line against the widest line instead.
            ImGui::SetTooltip("Width of the layout box.\n"
                              "0 = no box: no word wrap and no ellipsis. Horizontal\n"
                              "alignment then aligns each line against the widest line.");
        }

        if (ImGui::DragFloat("Rect Height##Text", &data.rectHeight, 1.0f, 0.0f, 10000.0f, "%.0f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Height of the layout box, used for Vertical alignment only.\n"
                              "0 = no box: the text is always top-aligned.\n"
                              "Nothing is clipped or truncated against it.");
        }

        if (ImGui::Checkbox("Word Wrap##Text", &data.wordWrap))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Off: Max Width still boxes alignment and ellipsis, but the\n"
                              "text runs past it on a single line.");
        }

        return changed;
    }
}
