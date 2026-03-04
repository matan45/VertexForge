#include "BillboardDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "nfd/FileDialog.hpp"
#include <imgui.h>
#include <fstream>

namespace windows::details
{
    bool BillboardDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasBillboardComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasBillboard = dispatcher.query(hasQuery);

        if (!hasBillboard)
        {
            return false;
        }

        events::scene::GetBillboardDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("BillboardComponent");

        bool removeBillboard = false;
        bool isOpen = drawHeader(removeBillboard);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::BillboardData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Camera-facing textured quad");
            ImGui::Spacing();

            changed |= drawTexturePath(data);
            ImGui::Spacing();
            changed |= drawRenderTextureSource(data);
            ImGui::Spacing();
            changed |= drawSizeInput(data);
            ImGui::Spacing();
            changed |= drawColorTint(data);

            if (changed)
            {
                events::scene::SetBillboardDataCommand cmd;
                cmd.entity = handle;
                cmd.billboardData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeBillboard)
        {
            events::scene::RemoveBillboardComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool BillboardDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##BillboardHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Billboard");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveBillboard", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool BillboardDrawer::drawTexturePath(services::BillboardData& data)
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

        if (ImGui::Button("Select Texture##Billboard"))
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
        if (ImGui::Button("Clear##BillboardTex"))
        {
            data.texturePath = "";
            changed = true;
        }
        if (wasEmpty) ImGui::EndDisabled();

        return changed;
    }

    bool BillboardDrawer::drawRenderTextureSource(services::BillboardData& data)
    {
        return rttPicker.draw("BillboardRTT", data.renderTextureSourceName, data.renderTextureSource);
    }

    bool BillboardDrawer::drawSizeInput(services::BillboardData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat2("Size##Billboard", &data.size.x, 0.01f, 0.01f, 100.0f, "%.2f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Billboard size in world units");
        }

        return changed;
    }

    bool BillboardDrawer::drawColorTint(services::BillboardData& data)
    {
        bool changed = false;

        if (ImGui::ColorEdit4("Color Tint##Billboard", &data.colorTint.x))
        {
            changed = true;
        }

        return changed;
    }
}
