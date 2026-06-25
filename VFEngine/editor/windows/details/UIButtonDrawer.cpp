#include "print/Log.hpp"
#include "UIButtonDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UIEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
#include "DrawerHelpers.hpp"
#include "UIDrawerCommon.hpp"
#include <imgui.h>
#include <fstream>

namespace windows::details
{
    bool UIButtonDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUIButtonComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasButton = dispatcher.query(hasQuery);

        if (!hasButton)
        {
            return false;
        }

        events::ui::GetUIButtonDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("UIButtonComponent");

        bool removeButton = false;
        bool isOpen = drawHeader(removeButton);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UIButtonData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Interactive button with state-based colors and textures");
            ImGui::Spacing();

            changed |= drawInteractable(data);
            ImGui::Spacing();

            changed |= drawStateColors(data);
            ImGui::Spacing();

            changed |= drawStateTextures(data);
            ImGui::Spacing();

            changed |= drawTransitionDuration(data);
            ImGui::Spacing();

            drawCurrentState(data);

            if (changed)
            {
                events::ui::SetUIButtonDataCommand cmd;
                cmd.entity = handle;
                cmd.buttonData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeButton)
        {
            events::ui::RemoveUIButtonComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UIButtonDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UIButtonHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Button");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUIButton", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool UIButtonDrawer::drawStateColors(services::UIButtonData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("State Colors", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ColorEditRow("Normal Color##UIButton", &data.normalColor.x))
            {
                changed = true;
            }

            if (ColorEditRow("Hovered Color##UIButton", &data.hoveredColor.x))
            {
                changed = true;
            }

            if (ColorEditRow("Pressed Color##UIButton", &data.pressedColor.x))
            {
                changed = true;
            }

            if (ColorEditRow("Disabled Color##UIButton", &data.disabledColor.x))
            {
                changed = true;
            }

            ImGui::TreePop();
        }

        return changed;
    }

    bool UIButtonDrawer::drawStateTextures(services::UIButtonData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("State Textures", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("Empty = color only mode");
            ImGui::Spacing();

            changed |= drawUITextureSlot("Normal", data.normalTextureRef, "UIBtn_normal");
            changed |= drawUITextureSlot("Hover", data.hoverTextureRef, "UIBtn_hover");
            changed |= drawUITextureSlot("Pressed", data.pressedTextureRef, "UIBtn_pressed");
            changed |= drawUITextureSlot("Disabled", data.disabledTextureRef, "UIBtn_disabled");

            ImGui::TreePop();
        }

        return changed;
    }

    bool UIButtonDrawer::drawTransitionDuration(services::UIButtonData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat("Transition Duration##UIButton", &data.colorTransitionDuration, 0.01f, 0.0f, 2.0f, "%.2f s"))
        {
            changed = true;
        }

        return changed;
    }

    bool UIButtonDrawer::drawInteractable(services::UIButtonData& data)
    {
        bool changed = false;

        if (ImGui::Checkbox("Interactable##UIButton", &data.interactable))
        {
            changed = true;
        }

        return changed;
    }

    void UIButtonDrawer::drawCurrentState(const services::UIButtonData& data)
    {
        const char* stateNames[] = {"Normal", "Hovered", "Pressed", "Disabled"};
        int stateIndex = static_cast<int>(data.currentState);
        const char* stateName = (stateIndex >= 0 && stateIndex <= 3) ? stateNames[stateIndex] : "Unknown";

        ImGui::Text("Current State:");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", stateName);
    }
}
