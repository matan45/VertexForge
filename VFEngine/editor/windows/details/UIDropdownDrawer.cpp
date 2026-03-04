#include "UIDropdownDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/UIEvents.hpp"
#include "nfd/FileDialog.hpp"
#include <imgui.h>
#include <fstream>

namespace windows::details
{
    bool UIDropdownDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUIDropdownComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasDropdown = dispatcher.query(hasQuery);

        if (!hasDropdown)
        {
            return false;
        }

        events::ui::GetUIDropdownDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("UIDropdownComponent");

        bool removeComponent = false;
        bool isOpen = drawHeader(removeComponent);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UIDropdownData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Dropdown / combo box with selectable options");
            ImGui::Spacing();

            changed |= drawInteractable(data);
            ImGui::Spacing();

            changed |= drawOptionsList(data);
            ImGui::Spacing();

            changed |= drawSelectedIndex(data);
            ImGui::Spacing();

            changed |= drawPlaceholderText(data);
            ImGui::Spacing();

            changed |= drawMaxVisibleItems(data);
            ImGui::Spacing();

            changed |= drawHeaderColors(data);
            ImGui::Spacing();

            changed |= drawListColors(data);
            ImGui::Spacing();

            changed |= drawFontSettings(data);
            ImGui::Spacing();

            changed |= drawTransitionDuration(data);
            ImGui::Spacing();

            drawCurrentState(data);

            if (changed)
            {
                events::ui::SetUIDropdownDataCommand cmd;
                cmd.entity = handle;
                cmd.dropdownData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeComponent)
        {
            events::ui::RemoveUIDropdownComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UIDropdownDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UIDropdownHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Dropdown");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUIDropdown", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool UIDropdownDrawer::drawInteractable(services::UIDropdownData& data)
    {
        bool changed = false;

        if (ImGui::Checkbox("Interactable##UIDropdown", &data.interactable))
        {
            changed = true;
        }

        return changed;
    }

    bool UIDropdownDrawer::drawOptionsList(services::UIDropdownData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("Options", ImGuiTreeNodeFlags_DefaultOpen))
        {
            int removeIndex = -1;
            int moveUpIndex = -1;
            int moveDownIndex = -1;

            for (int i = 0; i < static_cast<int>(data.options.size()); ++i)
            {
                ImGui::PushID(i);

                char textBuf[256];
                std::strncpy(textBuf, data.options[i].text.c_str(), sizeof(textBuf) - 1);
                textBuf[sizeof(textBuf) - 1] = '\0';

                char label[32];
                std::snprintf(label, sizeof(label), "##OptText%d", i);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120);
                if (ImGui::InputText(label, textBuf, sizeof(textBuf)))
                {
                    data.options[i].text = textBuf;
                    changed = true;
                }

                ImGui::SameLine();

                // Move up button
                bool isFirst = (i == 0);
                if (isFirst) ImGui::BeginDisabled();
                if (ImGui::Button("^##OptUp"))
                {
                    moveUpIndex = i;
                }
                if (isFirst) ImGui::EndDisabled();

                ImGui::SameLine();

                // Move down button
                bool isLast = (i == static_cast<int>(data.options.size()) - 1);
                if (isLast) ImGui::BeginDisabled();
                if (ImGui::Button("v##OptDown"))
                {
                    moveDownIndex = i;
                }
                if (isLast) ImGui::EndDisabled();

                ImGui::SameLine();

                // Remove button
                if (ImGui::Button("x##OptRemove"))
                {
                    removeIndex = i;
                }

                // Icon path (optional)
                if (!data.options[i].iconPath.empty())
                {
                    std::string filename = data.options[i].iconPath;
                    auto lastSlash = filename.find_last_of("/\\");
                    if (lastSlash != std::string::npos)
                        filename = filename.substr(lastSlash + 1);
                    ImGui::TextDisabled("  Icon: %s", filename.c_str());
                }

                char selectIconId[64];
                std::snprintf(selectIconId, sizeof(selectIconId), "Icon##OptIcon%d", i);
                if (ImGui::Button(selectIconId))
                {
                    nfd::FileDialog fileDialog;
                    std::string path = fileDialog.openFileDialog(
                        {{L"VF Image Files (*.vfImage)", L"*.vfImage"}});
                    if (!path.empty())
                    {
                        std::ifstream file(path);
                        if (file.good())
                        {
                            file.close();
                            data.options[i].iconPath = path;
                            changed = true;
                        }
                    }
                }

                ImGui::SameLine();

                bool noIcon = data.options[i].iconPath.empty();
                if (noIcon) ImGui::BeginDisabled();
                char clearIconId[64];
                std::snprintf(clearIconId, sizeof(clearIconId), "Clear##OptIconClr%d", i);
                if (ImGui::Button(clearIconId))
                {
                    data.options[i].iconPath = "";
                    changed = true;
                }
                if (noIcon) ImGui::EndDisabled();

                ImGui::Separator();
                ImGui::PopID();
            }

            // Handle reorder
            if (moveUpIndex > 0)
            {
                std::swap(data.options[moveUpIndex], data.options[moveUpIndex - 1]);
                if (data.selectedIndex == moveUpIndex)
                    data.selectedIndex = moveUpIndex - 1;
                else if (data.selectedIndex == moveUpIndex - 1)
                    data.selectedIndex = moveUpIndex;
                changed = true;
            }
            if (moveDownIndex >= 0 && moveDownIndex < static_cast<int>(data.options.size()) - 1)
            {
                std::swap(data.options[moveDownIndex], data.options[moveDownIndex + 1]);
                if (data.selectedIndex == moveDownIndex)
                    data.selectedIndex = moveDownIndex + 1;
                else if (data.selectedIndex == moveDownIndex + 1)
                    data.selectedIndex = moveDownIndex;
                changed = true;
            }

            // Handle remove
            if (removeIndex >= 0)
            {
                data.options.erase(data.options.begin() + removeIndex);
                if (data.selectedIndex >= static_cast<int>(data.options.size()))
                    data.selectedIndex = static_cast<int>(data.options.size()) - 1;
                changed = true;
            }

            // Add button
            if (ImGui::Button("+ Add Option##UIDropdown"))
            {
                data.options.push_back({"New Option", ""});
                changed = true;
            }

            ImGui::TreePop();
        }

        return changed;
    }

    bool UIDropdownDrawer::drawSelectedIndex(services::UIDropdownData& data)
    {
        bool changed = false;

        int maxIndex = static_cast<int>(data.options.size()) - 1;
        if (ImGui::DragInt("Selected Index##UIDropdown", &data.selectedIndex, 1.0f, -1, maxIndex, "%d"))
        {
            if (data.selectedIndex < -1) data.selectedIndex = -1;
            if (data.selectedIndex > maxIndex) data.selectedIndex = maxIndex;
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("-1 = no selection (shows placeholder)");
        }

        return changed;
    }

    bool UIDropdownDrawer::drawPlaceholderText(services::UIDropdownData& data)
    {
        bool changed = false;

        char placeholderBuf[256];
        std::strncpy(placeholderBuf, data.placeholderText.c_str(), sizeof(placeholderBuf) - 1);
        placeholderBuf[sizeof(placeholderBuf) - 1] = '\0';
        if (ImGui::InputText("Placeholder##UIDropdown", placeholderBuf, sizeof(placeholderBuf)))
        {
            data.placeholderText = placeholderBuf;
            changed = true;
        }

        return changed;
    }

    bool UIDropdownDrawer::drawMaxVisibleItems(services::UIDropdownData& data)
    {
        bool changed = false;

        if (ImGui::DragInt("Max Visible Items##UIDropdown", &data.maxVisibleItems, 1.0f, 1, 50, "%d"))
        {
            if (data.maxVisibleItems < 1) data.maxVisibleItems = 1;
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Number of visible items before scrolling");
        }

        return changed;
    }

    bool UIDropdownDrawer::drawHeaderColors(services::UIDropdownData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("Header State Colors", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::ColorEdit4("Normal Color##UIDropdownHeader", &data.normalColor.x))
                changed = true;
            if (ImGui::ColorEdit4("Hovered Color##UIDropdownHeader", &data.hoveredColor.x))
                changed = true;
            if (ImGui::ColorEdit4("Open Color##UIDropdownHeader", &data.openColor.x))
                changed = true;
            if (ImGui::ColorEdit4("Disabled Color##UIDropdownHeader", &data.disabledColor.x))
                changed = true;

            ImGui::TreePop();
        }

        return changed;
    }

    bool UIDropdownDrawer::drawListColors(services::UIDropdownData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("List Colors", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::ColorEdit4("Background##UIDropdownList", &data.listBackgroundColor.x))
                changed = true;
            if (ImGui::ColorEdit4("Item Normal##UIDropdownList", &data.itemNormalColor.x))
                changed = true;
            if (ImGui::ColorEdit4("Item Hovered##UIDropdownList", &data.itemHoveredColor.x))
                changed = true;

            ImGui::TreePop();
        }

        return changed;
    }

    bool UIDropdownDrawer::drawFontSettings(services::UIDropdownData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("Font##UIDropdown"))
        {
            if (!data.fontPath.empty())
            {
                std::string filename = data.fontPath;
                auto lastSlash = filename.find_last_of("/\\");
                if (lastSlash != std::string::npos)
                    filename = filename.substr(lastSlash + 1);
                ImGui::Text("Font: %s", filename.c_str());
            }
            else
            {
                ImGui::TextDisabled("No font selected");
            }

            if (ImGui::Button("Select Font##UIDropdown"))
            {
                nfd::FileDialog fileDialog;
                std::string path = fileDialog.openFileDialog(
                    {{L"VF Font Files (*.vfFont)", L"*.vfFont"}});
                if (!path.empty())
                {
                    std::ifstream file(path);
                    if (file.good())
                    {
                        file.close();
                        data.fontPath = path;
                        changed = true;
                    }
                }
            }

            if (ImGui::DragFloat("Font Size##UIDropdown", &data.fontSize, 0.5f, 1.0f, 200.0f, "%.1f"))
            {
                changed = true;
            }

            ImGui::TreePop();
        }

        return changed;
    }

    bool UIDropdownDrawer::drawTransitionDuration(services::UIDropdownData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat("Transition Duration##UIDropdown", &data.colorTransitionDuration, 0.01f, 0.0f, 2.0f, "%.2f s"))
        {
            changed = true;
        }

        return changed;
    }

    void UIDropdownDrawer::drawCurrentState(const services::UIDropdownData& data)
    {
        const char* stateNames[] = {"Normal", "Hovered", "Open", "Disabled"};
        int stateIndex = static_cast<int>(data.currentState);
        const char* stateName = (stateIndex >= 0 && stateIndex <= 3) ? stateNames[stateIndex] : "Unknown";

        ImGui::Text("Current State:");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", stateName);

        ImGui::Text("Is Open:");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", data.isOpen ? "Yes" : "No");

        if (data.selectedIndex >= 0 && data.selectedIndex < static_cast<int>(data.options.size()))
        {
            ImGui::Text("Selected:");
            ImGui::SameLine();
            ImGui::TextDisabled("[%d] %s", data.selectedIndex, data.options[data.selectedIndex].text.c_str());
        }
        else
        {
            ImGui::Text("Selected:");
            ImGui::SameLine();
            ImGui::TextDisabled("None");
        }
    }
}
