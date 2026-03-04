#include "print/Log.hpp"
#include "UICheckboxDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/UIEvents.hpp"
#include "nfd/FileDialog.hpp"
#include <imgui.h>
#include <fstream>

namespace windows::details
{
    bool UICheckboxDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUICheckboxComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasCheckbox = dispatcher.query(hasQuery);

        if (!hasCheckbox)
        {
            return false;
        }

        events::ui::GetUICheckboxDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("UICheckboxComponent");

        bool removeCheckbox = false;
        bool isOpen = drawHeader(removeCheckbox);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UICheckboxData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Toggleable checkbox with radio group support");
            ImGui::Spacing();

            changed |= drawCheckedState(data);
            changed |= drawInteractable(data);
            changed |= drawLabelToggle(data);
            ImGui::Spacing();

            changed |= drawGroupName(data);
            changed |= drawAllowUncheck(data);
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
                events::ui::SetUICheckboxDataCommand cmd;
                cmd.entity = handle;
                cmd.checkboxData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeCheckbox)
        {
            events::ui::RemoveUICheckboxComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UICheckboxDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UICheckboxHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Checkbox");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUICheckbox", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool UICheckboxDrawer::drawCheckedState(services::UICheckboxData& data)
    {
        bool changed = false;

        if (ImGui::Checkbox("Is Checked##UICheckbox", &data.isChecked))
        {
            changed = true;
        }

        return changed;
    }

    bool UICheckboxDrawer::drawGroupName(services::UICheckboxData& data)
    {
        bool changed = false;

        char buf[128];
        std::strncpy(buf, data.groupName.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        if (ImGui::InputText("Group Name##UICheckbox", buf, sizeof(buf)))
        {
            data.groupName = buf;
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Empty = independent checkbox. Non-empty = radio group (only one checked at a time)");
        }

        return changed;
    }

    bool UICheckboxDrawer::drawAllowUncheck(services::UICheckboxData& data)
    {
        if (data.groupName.empty())
        {
            return false;
        }

        bool changed = false;

        if (ImGui::Checkbox("Allow Uncheck##UICheckbox", &data.allowUncheck))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("When false, clicking a checked radio button does nothing (strict radio behavior)");
        }

        return changed;
    }

    bool UICheckboxDrawer::drawStateColors(services::UICheckboxData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("State Colors##UICheckbox", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::ColorEdit4("Unchecked Color##UICheckbox", &data.uncheckedColor.x))
            {
                changed = true;
            }

            if (ImGui::ColorEdit4("Checked Color##UICheckbox", &data.checkedColor.x))
            {
                changed = true;
            }

            if (ImGui::ColorEdit4("Hovered Color##UICheckbox", &data.hoveredColor.x))
            {
                changed = true;
            }

            if (ImGui::ColorEdit4("Disabled Color##UICheckbox", &data.disabledColor.x))
            {
                changed = true;
            }

            ImGui::TreePop();
        }

        return changed;
    }

    static bool drawCheckboxTextureSlot(const char* label, std::string& texturePath, const char* uniqueId)
    {
        bool changed = false;

        ImGui::Text("%s", label);

        if (!texturePath.empty())
        {
            std::string filename = texturePath;
            auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                filename = filename.substr(lastSlash + 1);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", filename.c_str());
        }

        char selectId[64];
        std::snprintf(selectId, sizeof(selectId), "Select##UIChk_%s", uniqueId);
        if (ImGui::Button(selectId))
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
                    texturePath = path;
                    changed = true;
                }
                else
                {
                    vfLogError("Selected texture file does not exist or cannot be read: {}", path);
                }
            }
        }

        ImGui::SameLine();
        bool wasEmpty = texturePath.empty();
        if (wasEmpty) ImGui::BeginDisabled();
        char clearId[64];
        std::snprintf(clearId, sizeof(clearId), "Clear##UIChk_%s", uniqueId);
        if (ImGui::Button(clearId))
        {
            texturePath = "";
            changed = true;
        }
        if (wasEmpty) ImGui::EndDisabled();

        return changed;
    }

    bool UICheckboxDrawer::drawStateTextures(services::UICheckboxData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("State Textures##UICheckbox", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("Empty = color only mode");
            ImGui::Spacing();

            changed |= drawCheckboxTextureSlot("Unchecked", data.uncheckedTexture, "unchecked");
            changed |= drawCheckboxTextureSlot("Checked", data.checkedTexture, "checked");
            changed |= drawCheckboxTextureSlot("Hovered", data.hoveredTexture, "hovered");
            changed |= drawCheckboxTextureSlot("Disabled", data.disabledTexture, "disabled");

            ImGui::TreePop();
        }

        return changed;
    }

    bool UICheckboxDrawer::drawTransitionDuration(services::UICheckboxData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat("Transition Duration##UICheckbox", &data.colorTransitionDuration, 0.01f, 0.0f, 2.0f, "%.2f s"))
        {
            changed = true;
        }

        return changed;
    }

    bool UICheckboxDrawer::drawInteractable(services::UICheckboxData& data)
    {
        bool changed = false;

        if (ImGui::Checkbox("Interactable##UICheckbox", &data.interactable))
        {
            changed = true;
        }

        return changed;
    }

    bool UICheckboxDrawer::drawLabelToggle(services::UICheckboxData& data)
    {
        bool changed = false;

        if (ImGui::Checkbox("Label Toggle##UICheckbox", &data.labelToggle))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("When enabled, clicking a child UILabel also toggles this checkbox");
        }

        return changed;
    }

    void UICheckboxDrawer::drawCurrentState(const services::UICheckboxData& data)
    {
        const char* stateNames[] = {"Normal", "Hovered", "Disabled"};
        int stateIndex = static_cast<int>(data.currentState);
        const char* stateName = (stateIndex >= 0 && stateIndex <= 2) ? stateNames[stateIndex] : "Unknown";

        ImGui::Text("Current State:");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", stateName);

        ImGui::Text("Checked:");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", data.isChecked ? "Yes" : "No");
    }
}
