#include "FogVolumeDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/FogVolumeEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool FogVolumeDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasFogVolumeComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasComponent = dispatcher.query(hasQuery);

        if (!hasComponent)
        {
            return false;
        }

        events::scene::GetFogVolumeDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("FogVolumeComponent");

        bool removeComponent = false;
        bool isOpen = drawHeader(removeComponent);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::FogVolumeData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Localized fog region with shape volume");
            ImGui::Spacing();

            changed |= drawShapeSettings(data);
            ImGui::Spacing();
            changed |= drawDensitySettings(data);
            ImGui::Spacing();
            changed |= drawBlendSettings(data);
            ImGui::Spacing();
            changed |= drawDebugSettings(data);

            if (changed)
            {
                events::scene::SetFogVolumeDataCommand cmd;
                cmd.entity = handle;
                cmd.data = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeComponent)
        {
            events::scene::RemoveFogVolumeComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool FogVolumeDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##FogVolumeHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Fog Volume");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveFogVolume", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool FogVolumeDrawer::drawShapeSettings(services::FogVolumeData& data)
    {
        bool changed = false;

        ImGui::Text("Shape:");
        ImGui::Indent(10.0f);

        const char* shapeNames[] = {"Box", "Sphere", "Cylinder"};
        int currentShape = static_cast<int>(data.shape);
        if (ImGui::Combo("Shape##FV", &currentShape, shapeNames, IM_ARRAYSIZE(shapeNames)))
        {
            data.shape = static_cast<uint8_t>(currentShape);
            changed = true;
        }

        if (ImGui::DragFloat3("Half Extents##FV", &data.halfExtents.x, 0.1f, 0.1f, 500.0f, "%.1f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Half-size of the fog volume in each axis");
        }

        ImGui::Unindent(10.0f);

        return changed;
    }

    bool FogVolumeDrawer::drawDensitySettings(services::FogVolumeData& data)
    {
        bool changed = false;

        ImGui::Text("Fog Properties:");
        ImGui::Indent(10.0f);

        if (ImGui::DragFloat("Density##FV", &data.density, 0.01f, 0.0f, 10.0f, "%.3f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Fog density within the volume");
        }

        if (ImGui::ColorEdit3("Albedo##FV", &data.albedo.x))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Scattering color of the fog");
        }

        if (ImGui::ColorEdit3("Emission##FV", &data.emission.x))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Self-illumination color (glowing fog)");
        }

        if (ImGui::SliderFloat("Edge Falloff##FV", &data.edgeFalloff, 0.0f, 1.0f, "%.2f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Soft transition at volume boundaries (0 = hard edge, 1 = full gradient)");
        }

        ImGui::Unindent(10.0f);

        return changed;
    }

    bool FogVolumeDrawer::drawBlendSettings(services::FogVolumeData& data)
    {
        bool changed = false;

        ImGui::Text("Blend:");
        ImGui::Indent(10.0f);

        const char* blendNames[] = {"Additive", "Subtractive"};
        int currentBlend = static_cast<int>(data.blendMode);
        if (ImGui::Combo("Blend Mode##FV", &currentBlend, blendNames, IM_ARRAYSIZE(blendNames)))
        {
            data.blendMode = static_cast<uint8_t>(currentBlend);
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Additive: adds fog density. Subtractive: carves out fog from the scene.");
        }

        ImGui::Unindent(10.0f);

        return changed;
    }

    bool FogVolumeDrawer::drawDebugSettings(services::FogVolumeData& data)
    {
        bool changed = false;

        if (ImGui::Checkbox("Show Gizmo##FV", &data.showGizmo))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Draw wireframe volume visualization in editor");
        }

        return changed;
    }
}
