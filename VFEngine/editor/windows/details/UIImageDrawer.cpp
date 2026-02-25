#include "UIImageDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/UIEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "print/EditorLogger.hpp"
#include <imgui.h>
#include <fstream>

namespace windows::details
{
    bool UIImageDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUIImageComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasImage = dispatcher.query(hasQuery);

        if (!hasImage)
        {
            return false;
        }

        events::ui::GetUIImageDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("UIImageComponent");

        bool removeImage = false;
        bool isOpen = drawHeader(removeImage);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UIImageData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Screen-space image with texture and color tint");
            ImGui::Spacing();

            changed |= drawTexturePath(data);
            ImGui::Spacing();
            changed |= drawRenderTextureSource(data);
            ImGui::Spacing();
            changed |= drawColorTint(data);

            if (changed)
            {
                events::ui::SetUIImageDataCommand cmd;
                cmd.entity = handle;
                cmd.imageData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeImage)
        {
            events::ui::RemoveUIImageComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UIImageDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UIImageHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Image");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUIImage", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool UIImageDrawer::drawTexturePath(services::UIImageData& data)
    {
        bool changed = false;

        if (!data.texturePath.empty())
        {
            std::string filename = data.texturePath;
            auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                filename = filename.substr(lastSlash + 1);
            }
            ImGui::Text("Texture: %s", filename.c_str());
        }
        else
        {
            ImGui::TextDisabled("No texture selected");
        }

        if (ImGui::Button("Select Texture##UIImage"))
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
                    data.texturePath = path;
                    changed = true;
                }
                else
                {
                    vfLogError("Selected texture file does not exist or cannot be read: {}", path);
                }
            }
        }

        ImGui::SameLine();
        bool wasEmpty = data.texturePath.empty();
        if (wasEmpty) ImGui::BeginDisabled();
        if (ImGui::Button("Clear##UIImageTex"))
        {
            data.texturePath = "";
            changed = true;
        }
        if (wasEmpty) ImGui::EndDisabled();

        return changed;
    }

    bool UIImageDrawer::drawRenderTextureSource(services::UIImageData& data)
    {
        bool changed = false;

        if (rttNeedsRefresh)
        {
            refreshRTTCandidates();
            rttNeedsRefresh = false;

            // Sync selectedRTTIdx with current data
            selectedRTTIdx = -1;
            if (!data.renderTextureSourceName.empty())
            {
                for (int i = 0; i < static_cast<int>(rttCandidateNames.size()); ++i)
                {
                    if (rttCandidateNames[i] == data.renderTextureSourceName)
                    {
                        selectedRTTIdx = i;
                        break;
                    }
                }
            }
        }

        const char* preview = selectedRTTIdx >= 0 && selectedRTTIdx < static_cast<int>(rttCandidateNames.size())
            ? rttCandidateNames[selectedRTTIdx].c_str()
            : "None";

        if (ImGui::BeginCombo("RTT Source##UIImage", preview))
        {
            // "None" option
            if (ImGui::Selectable("None##UIImageRTT", selectedRTTIdx < 0))
            {
                selectedRTTIdx = -1;
                data.renderTextureSourceName = "";
                data.renderTextureSource = services::EntityHandle::invalid();
                changed = true;
            }

            for (int i = 0; i < static_cast<int>(rttCandidates.size()); ++i)
            {
                bool isSelected = (i == selectedRTTIdx);
                if (ImGui::Selectable(rttCandidateNames[i].c_str(), isSelected))
                {
                    selectedRTTIdx = i;
                    data.renderTextureSourceName = rttCandidateNames[i];
                    data.renderTextureSource = rttCandidates[i];
                    changed = true;
                }
                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Entity with RenderTextureComponent + CameraComponent.\nDisplays camera feed on this image during play mode.");
        }

        ImGui::SameLine();
        if (ImGui::Button("Refresh##UIImageRTT"))
        {
            refreshRTTCandidates();

            // Re-sync selection with current data
            selectedRTTIdx = -1;
            if (!data.renderTextureSourceName.empty())
            {
                for (int i = 0; i < static_cast<int>(rttCandidateNames.size()); ++i)
                {
                    if (rttCandidateNames[i] == data.renderTextureSourceName)
                    {
                        selectedRTTIdx = i;
                        break;
                    }
                }
            }
        }

        return changed;
    }

    void UIImageDrawer::refreshRTTCandidates()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        rttCandidates.clear();
        rttCandidateNames.clear();
        selectedRTTIdx = -1;

        events::scene::GetEntitiesWithComponentQuery compQuery;
        compQuery.componentType = services::ComponentTypeId::RenderTexture;
        rttCandidates = dispatcher.query(compQuery);

        for (const auto& candidate : rttCandidates)
        {
            events::scene::GetEntityQuery entityQuery;
            entityQuery.entity = candidate;
            auto entityDataOpt = dispatcher.query(entityQuery);

            if (entityDataOpt.has_value() && !entityDataOpt->name.empty())
            {
                rttCandidateNames.push_back(entityDataOpt->name);
            }
            else
            {
                rttCandidateNames.push_back("Entity #" + std::to_string(candidate.id));
            }
        }
    }

    bool UIImageDrawer::drawColorTint(services::UIImageData& data)
    {
        bool changed = false;

        if (ImGui::ColorEdit4("Color Tint##UIImage", &data.colorTint.x))
        {
            changed = true;
        }

        return changed;
    }
}
