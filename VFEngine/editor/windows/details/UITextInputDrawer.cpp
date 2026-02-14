#include "UITextInputDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/UIEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "print/EditorLogger.hpp"
#include <imgui.h>
#include <fstream>

namespace windows::details
{
    bool UITextInputDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUITextInputComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasTextInput = dispatcher.query(hasQuery);

        if (!hasTextInput)
        {
            return false;
        }

        events::ui::GetUITextInputDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("UITextInputComponent");

        bool removeComponent = false;
        bool isOpen = drawHeader(removeComponent);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UITextInputData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Editable text input field with focus and selection");
            ImGui::Spacing();

            changed |= drawInteractable(data);
            ImGui::Spacing();

            changed |= drawTextFields(data);
            ImGui::Spacing();

            changed |= drawFontSettings(data);
            ImGui::Spacing();

            changed |= drawMaxLength(data);
            ImGui::Spacing();

            changed |= drawStateColors(data);
            ImGui::Spacing();

            changed |= drawCaretSettings(data);
            ImGui::Spacing();

            changed |= drawSelectionColor(data);
            ImGui::Spacing();

            changed |= drawTransitionDuration(data);
            ImGui::Spacing();

            drawCurrentState(data);

            if (changed)
            {
                events::ui::SetUITextInputDataCommand cmd;
                cmd.entity = handle;
                cmd.textInputData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeComponent)
        {
            events::ui::RemoveUITextInputComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UITextInputDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UITextInputHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Text Input");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUITextInput", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool UITextInputDrawer::drawTextFields(services::UITextInputData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("Text", ImGuiTreeNodeFlags_DefaultOpen))
        {
            char textBuf[1024];
            std::strncpy(textBuf, data.text.c_str(), sizeof(textBuf) - 1);
            textBuf[sizeof(textBuf) - 1] = '\0';
            if (ImGui::InputText("Text##UITextInput", textBuf, sizeof(textBuf)))
            {
                data.text = textBuf;
                changed = true;
            }

            char placeholderBuf[512];
            std::strncpy(placeholderBuf, data.placeholderText.c_str(), sizeof(placeholderBuf) - 1);
            placeholderBuf[sizeof(placeholderBuf) - 1] = '\0';
            if (ImGui::InputText("Placeholder##UITextInput", placeholderBuf, sizeof(placeholderBuf)))
            {
                data.placeholderText = placeholderBuf;
                changed = true;
            }

            ImGui::TreePop();
        }

        return changed;
    }

    bool UITextInputDrawer::drawFontSettings(services::UITextInputData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("Font", ImGuiTreeNodeFlags_DefaultOpen))
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

            if (ImGui::Button("Select Font##UITextInput"))
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

            if (ImGui::DragFloat("Font Size##UITextInput", &data.fontSize, 0.5f, 1.0f, 200.0f, "%.1f"))
            {
                changed = true;
            }

            if (ImGui::ColorEdit4("Text Color##UITextInput", &data.textColor.x))
            {
                changed = true;
            }

            if (ImGui::ColorEdit4("Placeholder Color##UITextInput", &data.placeholderColor.x))
            {
                changed = true;
            }

            ImGui::TreePop();
        }

        return changed;
    }

    bool UITextInputDrawer::drawStateColors(services::UITextInputData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("State Colors", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::ColorEdit4("Normal Color##UITextInput", &data.normalColor.x))
                changed = true;
            if (ImGui::ColorEdit4("Hovered Color##UITextInput", &data.hoveredColor.x))
                changed = true;
            if (ImGui::ColorEdit4("Focused Color##UITextInput", &data.focusedColor.x))
                changed = true;
            if (ImGui::ColorEdit4("Disabled Color##UITextInput", &data.disabledColor.x))
                changed = true;

            ImGui::TreePop();
        }

        return changed;
    }

    bool UITextInputDrawer::drawCaretSettings(services::UITextInputData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("Caret##UITextInput"))
        {
            if (ImGui::ColorEdit4("Caret Color##UITextInput", &data.caretColor.x))
                changed = true;
            if (ImGui::DragFloat("Caret Width##UITextInput", &data.caretWidth, 0.1f, 0.5f, 10.0f, "%.1f"))
                changed = true;
            if (ImGui::DragFloat("Blink Rate##UITextInput", &data.caretBlinkRate, 0.01f, 0.1f, 2.0f, "%.2f s"))
                changed = true;

            ImGui::TreePop();
        }

        return changed;
    }

    bool UITextInputDrawer::drawSelectionColor(services::UITextInputData& data)
    {
        bool changed = false;

        if (ImGui::ColorEdit4("Selection Color##UITextInput", &data.selectionColor.x))
            changed = true;

        return changed;
    }

    bool UITextInputDrawer::drawTransitionDuration(services::UITextInputData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat("Transition Duration##UITextInput", &data.colorTransitionDuration, 0.01f, 0.0f, 2.0f, "%.2f s"))
            changed = true;

        return changed;
    }

    bool UITextInputDrawer::drawInteractable(services::UITextInputData& data)
    {
        bool changed = false;

        if (ImGui::Checkbox("Interactable##UITextInput", &data.interactable))
            changed = true;

        return changed;
    }

    bool UITextInputDrawer::drawMaxLength(services::UITextInputData& data)
    {
        bool changed = false;

        if (ImGui::DragInt("Max Length##UITextInput", &data.maxLength, 1.0f, 0, 10000, "%d"))
        {
            if (data.maxLength < 0) data.maxLength = 0;
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("0 = unlimited");
        }

        return changed;
    }

    void UITextInputDrawer::drawCurrentState(const services::UITextInputData& data)
    {
        const char* stateNames[] = {"Normal", "Hovered", "Focused", "Disabled"};
        int stateIndex = static_cast<int>(data.currentState);
        const char* stateName = (stateIndex >= 0 && stateIndex <= 3) ? stateNames[stateIndex] : "Unknown";

        ImGui::Text("Current State:");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", stateName);
    }
}
