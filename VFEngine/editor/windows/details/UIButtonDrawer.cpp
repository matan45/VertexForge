#include "UIButtonDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/UIEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "print/EditorLogger.hpp"
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
            if (ImGui::ColorEdit4("Normal Color##UIButton", &data.normalColor.x))
            {
                changed = true;
            }

            if (ImGui::ColorEdit4("Hovered Color##UIButton", &data.hoveredColor.x))
            {
                changed = true;
            }

            if (ImGui::ColorEdit4("Pressed Color##UIButton", &data.pressedColor.x))
            {
                changed = true;
            }

            if (ImGui::ColorEdit4("Disabled Color##UIButton", &data.disabledColor.x))
            {
                changed = true;
            }

            ImGui::TreePop();
        }

        return changed;
    }

    static bool drawTextureSlot(const char* label, std::string& texturePath, const char* uniqueId)
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
        std::snprintf(selectId, sizeof(selectId), "Select##UIBtn_%s", uniqueId);
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
        std::snprintf(clearId, sizeof(clearId), "Clear##UIBtn_%s", uniqueId);
        if (ImGui::Button(clearId))
        {
            texturePath = "";
            changed = true;
        }
        if (wasEmpty) ImGui::EndDisabled();

        return changed;
    }

    bool UIButtonDrawer::drawStateTextures(services::UIButtonData& data)
    {
        bool changed = false;

        if (ImGui::TreeNodeEx("State Textures", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("Empty = color only mode");
            ImGui::Spacing();

            changed |= drawTextureSlot("Normal", data.normalTexture, "normal");
            changed |= drawTextureSlot("Hover", data.hoverTexture, "hover");
            changed |= drawTextureSlot("Pressed", data.pressedTexture, "pressed");
            changed |= drawTextureSlot("Disabled", data.disabledTexture, "disabled");

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
