#include "UIScrollDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/UIEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool UIScrollDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUIScrollComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasScroll = dispatcher.query(hasQuery);

        if (!hasScroll)
        {
            return false;
        }

        events::ui::GetUIScrollDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("UIScrollComponent");

        bool removeScroll = false;
        bool isOpen = drawHeader(removeScroll);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UIScrollData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Scroll container with clipping and scrollbars");
            ImGui::Spacing();

            changed |= drawScrollEnabled(data);
            ImGui::Spacing();
            changed |= drawScrollbarVisibility(data);
            ImGui::Spacing();
            changed |= drawSensitivity(data);

            if (changed)
            {
                events::ui::SetUIScrollDataCommand cmd;
                cmd.entity = handle;
                cmd.scrollData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Spacing();
            ImGui::Separator();
            drawDebugInfo(handle);

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeScroll)
        {
            events::ui::RemoveUIScrollComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UIScrollDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UIScrollHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Scroll");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUIScroll", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool UIScrollDrawer::drawScrollEnabled(services::UIScrollData& data)
    {
        bool changed = false;

        if (ImGui::Checkbox("Horizontal Scroll##UIScroll", &data.horizontalScrollEnabled))
        {
            changed = true;
        }

        if (ImGui::Checkbox("Vertical Scroll##UIScroll", &data.verticalScrollEnabled))
        {
            changed = true;
        }

        return changed;
    }

    bool UIScrollDrawer::drawScrollbarVisibility(services::UIScrollData& data)
    {
        bool changed = false;

        const char* modes[] = {"Auto", "Always Visible", "Hidden"};

        int hVis = static_cast<int>(data.horizontalScrollbarVisibility);
        if (ImGui::Combo("H Scrollbar##UIScroll", &hVis, modes, 3))
        {
            data.horizontalScrollbarVisibility = static_cast<uint8_t>(hVis);
            changed = true;
        }

        int vVis = static_cast<int>(data.verticalScrollbarVisibility);
        if (ImGui::Combo("V Scrollbar##UIScroll", &vVis, modes, 3))
        {
            data.verticalScrollbarVisibility = static_cast<uint8_t>(vVis);
            changed = true;
        }

        return changed;
    }

    bool UIScrollDrawer::drawSensitivity(services::UIScrollData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat("Sensitivity##UIScroll", &data.scrollSensitivity, 0.1f, 0.1f, 10.0f, "%.1f"))
        {
            changed = true;
        }

        return changed;
    }

    void UIScrollDrawer::drawDebugInfo(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::TextDisabled("Runtime (read-only)");

        events::ui::GetScrollOffsetQuery offsetQuery;
        offsetQuery.entity = handle;
        auto offsetOpt = dispatcher.query(offsetQuery);

        if (offsetOpt.has_value())
        {
            glm::vec2 offset = *offsetOpt;
            ImGui::Text("Scroll Offset: %.1f, %.1f", offset.x, offset.y);
        }
        else
        {
            ImGui::Text("Scroll Offset: 0.0, 0.0");
        }
    }
}
