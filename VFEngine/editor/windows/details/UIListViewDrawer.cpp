#include "UIListViewDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UIListViewEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
#include <imgui.h>

namespace windows::details
{
    bool UIListViewDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUIListViewComponentQuery hasQuery;
        hasQuery.entity = handle;
        if (!dispatcher.query(hasQuery))
            return false;

        events::ui::GetUIListViewDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);
        if (!dataOpt.has_value())
            return true;

        ImGui::PushID("UIListViewComponent");

        bool removeListView = false;
        bool isOpen = drawHeader(removeListView);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UIListViewData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Instantiates N copies of a .vfPrefab item template");
            ImGui::Spacing();

            if (data.itemTemplateRef.isValid())
            {
                std::string filename = data.itemTemplateRef.resolve();
                auto lastSlash = filename.find_last_of("/\\");
                if (lastSlash != std::string::npos)
                    filename = filename.substr(lastSlash + 1);
                ImGui::Text("Template: %s", filename.c_str());
            }
            else
            {
                ImGui::TextDisabled("No item template selected");
            }

            if (ImGui::Button("Select Template##UIListView"))
            {
                nfd::FileDialog fileDialog;
                std::string path = fileDialog.openFileDialog(
                    {{L"VF Prefab Files (*.vfPrefab)", L"*.vfPrefab"}});
                if (!path.empty())
                {
                    events::ui::SetUIListItemTemplateCommand cmd;
                    cmd.entity = handle;
                    cmd.templatePath = path;
                    dispatcher.execute(cmd);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Rebuild##UIListView"))
            {
                events::ui::SetUIListItemCountCommand cmd;
                cmd.entity = handle;
                cmd.itemCount = data.itemCount;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Re-instantiate item instances from the template");

            int itemCount = data.itemCount;
            if (ImGui::DragInt("Item Count##UIListView", &itemCount, 0.2f, 0, 256))
            {
                events::ui::SetUIListItemCountCommand cmd;
                cmd.entity = handle;
                cmd.itemCount = itemCount;
                dispatcher.execute(cmd);
            }

            changed |= ImGui::Checkbox("Selectable##UIListView", &data.selectable);
            if (data.selectable)
            {
                changed |= ImGui::ColorEdit4("Selected Tint##UIListView", &data.selectedTint.x);
                ImGui::TextDisabled("Selected: %d", data.selectedIndex);
            }

            if (changed)
            {
                events::ui::SetUIListViewDataCommand cmd;
                cmd.entity = handle;
                cmd.listViewData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeListView)
        {
            events::ui::RemoveUIListViewComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UIListViewDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UIListViewHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI List View");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUIListView", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }
}
