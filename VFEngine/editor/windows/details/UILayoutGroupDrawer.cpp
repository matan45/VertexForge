#include "UILayoutGroupDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UIEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool UILayoutGroupDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUILayoutGroupComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasLayoutGroup = dispatcher.query(hasQuery);

        if (!hasLayoutGroup)
        {
            return false;
        }

        events::ui::GetUILayoutGroupDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("UILayoutGroupComponent");

        bool removeLayoutGroup = false;
        bool isOpen = drawHeader(removeLayoutGroup);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UILayoutGroupData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Auto-layout children in stack or grid");
            ImGui::Spacing();

            changed |= drawDirection(data);
            if (data.direction == 2) // Grid
            {
                ImGui::Spacing();
                changed |= drawConstraintCount(data);
            }
            ImGui::Spacing();
            changed |= drawSpacing(data);
            ImGui::Spacing();
            changed |= drawPadding(data);
            ImGui::Spacing();
            changed |= drawChildAlignment(data);

            if (changed)
            {
                events::ui::SetUILayoutGroupDataCommand cmd;
                cmd.entity = handle;
                cmd.layoutGroupData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeLayoutGroup)
        {
            events::ui::RemoveUILayoutGroupComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UILayoutGroupDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UILayoutGroupHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Layout Group");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUILayoutGroup", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool UILayoutGroupDrawer::drawDirection(services::UILayoutGroupData& data)
    {
        bool changed = false;

        const char* directions[] = {"Vertical", "Horizontal", "Grid"};
        int dir = static_cast<int>(data.direction);
        if (ImGui::Combo("Direction##UILayoutGroup", &dir, directions, 3))
        {
            data.direction = static_cast<uint8_t>(dir);
            changed = true;
        }

        return changed;
    }

    bool UILayoutGroupDrawer::drawSpacing(services::UILayoutGroupData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat("Spacing##UILayoutGroup", &data.spacing, 0.5f, 0.0f, 500.0f, "%.1f"))
        {
            changed = true;
        }

        return changed;
    }

    bool UILayoutGroupDrawer::drawPadding(services::UILayoutGroupData& data)
    {
        bool changed = false;

        ImGui::Text("Padding");
        if (ImGui::DragFloat("Left##UILayoutGroupPad", &data.padding.x, 0.5f, 0.0f, 500.0f, "%.1f"))
        {
            changed = true;
        }
        if (ImGui::DragFloat("Right##UILayoutGroupPad", &data.padding.y, 0.5f, 0.0f, 500.0f, "%.1f"))
        {
            changed = true;
        }
        if (ImGui::DragFloat("Top##UILayoutGroupPad", &data.padding.z, 0.5f, 0.0f, 500.0f, "%.1f"))
        {
            changed = true;
        }
        if (ImGui::DragFloat("Bottom##UILayoutGroupPad", &data.padding.w, 0.5f, 0.0f, 500.0f, "%.1f"))
        {
            changed = true;
        }

        return changed;
    }

    bool UILayoutGroupDrawer::drawChildAlignment(services::UILayoutGroupData& data)
    {
        bool changed = false;

        const char* alignments[] = {"Start", "Center", "End"};
        int align = static_cast<int>(data.childAlignment);
        if (ImGui::Combo("Child Alignment##UILayoutGroup", &align, alignments, 3))
        {
            data.childAlignment = static_cast<uint8_t>(align);
            changed = true;
        }

        return changed;
    }

    bool UILayoutGroupDrawer::drawConstraintCount(services::UILayoutGroupData& data)
    {
        bool changed = false;

        if (ImGui::DragInt("Columns##UILayoutGroup", &data.constraintCount, 0.1f, 1, 20))
        {
            if (data.constraintCount < 1) data.constraintCount = 1;
            changed = true;
        }

        return changed;
    }
}
