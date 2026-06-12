#include "UIWindowDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UIWindowEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
#include <imgui.h>
#include <cstring>

namespace windows::details
{
    bool UIWindowDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUIWindowComponentQuery hasQuery;
        hasQuery.entity = handle;
        if (!dispatcher.query(hasQuery))
            return false;

        events::ui::GetUIWindowDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);
        if (!dataOpt.has_value())
            return true;

        ImGui::PushID("UIWindowComponent");

        bool removeWindow = false;
        bool isOpen = drawHeader(removeWindow);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UIWindowData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Visibility = entity active state (UI::openWindow/closeWindow)");
            ImGui::Spacing();

            char buffer[256] = {};
            std::strncpy(buffer, data.title.c_str(), sizeof(buffer) - 1);
            if (ImGui::InputText("Title##UIWindow", buffer, sizeof(buffer)))
            {
                data.title = buffer;
                changed = true;
            }

            changed |= ImGui::Checkbox("Title Bar##UIWindow", &data.showTitleBar);
            if (data.showTitleBar)
            {
                changed |= ImGui::DragFloat("Title Bar Height##UIWindow", &data.titleBarHeight, 0.5f, 12.0f, 96.0f, "%.0f");
                changed |= ImGui::Checkbox("Draggable##UIWindow", &data.draggable);
                changed |= ImGui::Checkbox("Closable##UIWindow", &data.closable);
            }
            changed |= ImGui::Checkbox("Modal##UIWindow", &data.modal);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Dim backdrop; blocks interaction with everything beneath");

            changed |= ImGui::ColorEdit4("Background##UIWindow", &data.backgroundColor.x);
            changed |= ImGui::ColorEdit4("Title Bar Color##UIWindow", &data.titleBarColor.x);
            changed |= ImGui::ColorEdit4("Title Text##UIWindow", &data.titleTextColor.x);
            if (data.modal)
            {
                changed |= ImGui::ColorEdit4("Backdrop##UIWindow", &data.backdropColor.x);
            }
            changed |= ImGui::DragFloat("Title Font Size##UIWindow", &data.titleFontSize, 0.5f, 6.0f, 96.0f, "%.0f");

            if (data.fontRef.isValid())
            {
                std::string filename = data.fontRef.resolve();
                auto lastSlash = filename.find_last_of("/\\");
                if (lastSlash != std::string::npos)
                    filename = filename.substr(lastSlash + 1);
                ImGui::Text("Font: %s", filename.c_str());
            }
            else
            {
                ImGui::TextDisabled("No font selected (title text needs one)");
            }
            if (ImGui::Button("Select Font##UIWindow"))
            {
                nfd::FileDialog fileDialog;
                std::string path = fileDialog.openFileDialog(
                    {{L"VF Font Files (*.vfFont)", L"*.vfFont"}});
                if (!path.empty())
                {
                    data.fontRef = asset::AssetRef::fromPath(path);
                    changed = true;
                }
            }

            if (changed)
            {
                events::ui::SetUIWindowDataCommand cmd;
                cmd.entity = handle;
                cmd.windowData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeWindow)
        {
            events::ui::RemoveUIWindowComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UIWindowDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UIWindowHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Window");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUIWindow", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }
}
