#include "UITabsDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/UIEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool UITabsDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUITabsComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasTabs = dispatcher.query(hasQuery);

        if (!hasTabs)
        {
            return false;
        }

        events::ui::GetUITabsDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("UITabsComponent");

        bool removeTabs = false;
        bool isOpen = drawHeader(removeTabs);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UITabsData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Tabbed panel container with switchable content views");
            ImGui::Spacing();

            changed |= drawTabBarPosition(data);
            changed |= drawActiveTabIndex(data);
            ImGui::Spacing();

            drawCurrentState(data);

            if (changed)
            {
                events::ui::SetUITabsDataCommand cmd;
                cmd.entity = handle;
                cmd.tabsData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeTabs)
        {
            events::ui::RemoveUITabsComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UITabsDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UITabsHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Tabs");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUITabs", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool UITabsDrawer::drawTabBarPosition(services::UITabsData& data)
    {
        bool changed = false;

        const char* positions[] = {"Top", "Bottom", "Left", "Right"};
        int currentPos = static_cast<int>(data.tabBarPosition);
        if (currentPos < 0 || currentPos > 3) currentPos = 0;

        if (ImGui::Combo("Tab Bar Position##UITabs", &currentPos, positions, 4))
        {
            data.tabBarPosition = static_cast<uint8_t>(currentPos);
            changed = true;
        }

        return changed;
    }

    bool UITabsDrawer::drawActiveTabIndex(services::UITabsData& data)
    {
        bool changed = false;

        int activeIndex = data.activeTabIndex;
        if (ImGui::InputInt("Active Tab Index##UITabs", &activeIndex))
        {
            if (activeIndex < 0) activeIndex = 0;
            data.activeTabIndex = activeIndex;
            changed = true;
        }

        return changed;
    }

    void UITabsDrawer::drawCurrentState(const services::UITabsData& data)
    {
        if (ImGui::TreeNodeEx("Runtime State##UITabs", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("Previous Tab Index: %d", data.previousTabIndex);
            ImGui::TreePop();
        }
    }
}
