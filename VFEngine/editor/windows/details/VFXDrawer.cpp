#include "VFXDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "print/EditorLogger.hpp"
#include <imgui.h>
#include <fstream>

namespace windows::details
{
    bool VFXDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasVFXComponentQuery hasVFXQuery;
        hasVFXQuery.entity = handle;
        bool hasVFX = dispatcher.query(hasVFXQuery);

        if (!hasVFX)
        {
            return false;
        }

        events::scene::GetVFXDataQuery vfxQuery;
        vfxQuery.entity = handle;
        auto vfxOpt = dispatcher.query(vfxQuery);

        if (!vfxOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("VFXComponent");

        bool removeVFX = false;
        bool isOpen = drawHeader(removeVFX);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::VFXData vfxData = *vfxOpt;
            bool changed = false;

            ImGui::TextDisabled("Visual effects particle system");
            ImGui::Spacing();

            changed |= drawVFXFilePath(vfxData);
            ImGui::Spacing();
            changed |= drawSettings(vfxData);

            if (changed)
            {
                events::scene::SetVFXDataCommand cmd;
                cmd.entity = handle;
                cmd.vfxData = vfxData;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeVFX)
        {
            events::scene::RemoveVFXComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool VFXDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##VFXHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("VFX");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveVFX", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool VFXDrawer::drawVFXFilePath(services::VFXData& vfxData)
    {
        bool changed = false;

        if (!vfxData.vfxPath.empty())
        {
            std::string filename = vfxData.vfxPath;
            auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                filename = filename.substr(lastSlash + 1);
            }
            ImGui::Text("File: %s", filename.c_str());
        }
        else
        {
            ImGui::TextDisabled("No VFX file selected");
        }

        if (ImGui::Button("Select VFX File##VFX"))
        {
            nfd::FileDialog fileDialog;
            std::string path = fileDialog.openFileDialog(
                {{L"VF VFX Files (*.vfVFX)", L"*.vfVFX"}});
            if (!path.empty())
            {
                std::ifstream file(path);
                if (file.good())
                {
                    file.close();
                    vfxData.vfxPath = path;
                    changed = true;
                }
                else
                {
                    vfLogError("Selected VFX file does not exist or cannot be read: {}", path);
                }
            }
        }

        ImGui::SameLine();
        bool wasEmpty = vfxData.vfxPath.empty();
        if (wasEmpty) ImGui::BeginDisabled();
        if (ImGui::Button("Clear##VFX"))
        {
            vfxData.vfxPath = "";
            changed = true;
        }
        if (wasEmpty) ImGui::EndDisabled();

        return changed;
    }

    bool VFXDrawer::drawSettings(services::VFXData& vfxData)
    {
        bool changed = false;

        if (ImGui::Checkbox("Auto Play##VFX", &vfxData.autoPlay))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Automatically play when entering play mode");
        }

        if (ImGui::Checkbox("Loop##VFX", &vfxData.loop))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Loop the VFX effect continuously");
        }

        return changed;
    }
}
