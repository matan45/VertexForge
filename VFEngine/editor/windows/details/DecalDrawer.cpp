#include "DecalDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
#include <imgui.h>
#include <fstream>

namespace windows::details
{
    bool DecalDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasDecalComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasDecal = dispatcher.query(hasQuery);

        if (!hasDecal)
        {
            return false;
        }

        events::scene::GetDecalDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("DecalComponent");

        bool removeDecal = false;
        bool isOpen = drawHeader(removeDecal);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::DecalData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Projects textures onto scene geometry");
            ImGui::Spacing();

            changed |= drawHalfExtents(data);
            ImGui::Spacing();
            changed |= drawTextures(data);
            ImGui::Spacing();
            changed |= drawColor(data);
            ImGui::Spacing();
            changed |= drawFadeSettings(data);
            ImGui::Spacing();
            changed |= drawAdvancedSettings(data);

            if (changed)
            {
                events::scene::SetDecalDataCommand cmd;
                cmd.entity = handle;
                cmd.decalData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeDecal)
        {
            events::scene::RemoveDecalComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool DecalDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##DecalHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Decal");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveDecal", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool DecalDrawer::drawHalfExtents(services::DecalData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat3("Half Extents##Decal", &data.halfExtents.x, 0.01f, 0.01f, 50.0f, "%.3f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("OBB half extents: width, height, depth of projection volume");
        }

        return changed;
    }

    bool DecalDrawer::drawTextureSlot(const char* label, const char* emptyText, asset::AssetRef& textureRef)
    {
        bool changed = false;

        if (textureRef.isValid())
        {
            std::string filename = textureRef.resolve();
            auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
                filename = filename.substr(lastSlash + 1);
            ImGui::Text("%s: %s", label, filename.c_str());
        }
        else
        {
            ImGui::TextDisabled("%s", emptyText);
        }

        ImGui::PushID(label);
        if (ImGui::Button("Select##Decal"))
        {
            nfd::FileDialog fileDialog;
            std::string path = fileDialog.openFileDialog(
                {{L"VF Image Files (*.vfImage)", L"*.vfImage"}});
            if (!path.empty())
            {
                textureRef = asset::AssetRef::fromPath(path);
                changed = true;
            }
        }
        ImGui::SameLine();
        if (textureRef.isValid())
        {
            if (ImGui::Button("Clear##Decal"))
            {
                textureRef = asset::AssetRef::invalid();
                changed = true;
            }
        }
        ImGui::PopID();

        return changed;
    }

    bool DecalDrawer::drawTextures(services::DecalData& data)
    {
        bool changed = false;

        changed |= drawTextureSlot("Albedo", "No albedo texture", data.albedoTextureRef);
        ImGui::Spacing();
        changed |= drawTextureSlot("Normal", "No normal map", data.normalTextureRef);
        ImGui::Spacing();
        changed |= drawTextureSlot("ORM", "No ORM texture", data.ormTextureRef);

        return changed;
    }

    bool DecalDrawer::drawColor(services::DecalData& data)
    {
        bool changed = false;

        if (ImGui::ColorEdit4("Color##Decal", &data.color.x))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Decal tint color and opacity");
        }

        return changed;
    }

    bool DecalDrawer::drawFadeSettings(services::DecalData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat("Angle Fade Start##Decal", &data.angleFadeStart, 0.01f, 0.0f, 1.0f, "%.2f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Cosine angle above which decal is fully visible (0=parallel, 1=perpendicular)");
        }

        if (ImGui::DragFloat("Angle Fade End##Decal", &data.angleFadeEnd, 0.01f, 0.0f, 1.0f, "%.2f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Cosine angle below which decal is fully faded out");
        }

        if (ImGui::DragFloat("Edge Falloff##Decal", &data.edgeFalloff, 0.01f, 0.0f, 1.0f, "%.2f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Soft edge fade distance at OBB boundaries");
        }

        return changed;
    }

    bool DecalDrawer::drawAdvancedSettings(services::DecalData& data)
    {
        bool changed = false;

        if (ImGui::DragInt("Sort Priority##Decal", &data.sortPriority, 1, -100, 100))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Higher priority decals render on top of lower ones");
        }

        if (ImGui::Checkbox("Modify Normals##Decal", &data.modifyNormals))
        {
            changed = true;
        }

        if (data.modifyNormals)
        {
            if (ImGui::DragFloat("Normal Strength##Decal", &data.normalStrength, 0.01f, 0.0f, 2.0f, "%.2f"))
            {
                changed = true;
            }
        }

        return changed;
    }
}
