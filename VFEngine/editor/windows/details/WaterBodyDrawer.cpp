#include "WaterBodyDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/OceanEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool WaterBodyDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ocean::HasWaterBodyComponentQuery hasQuery;
        hasQuery.entity = handle;
        if (!dispatcher.query(hasQuery))
        {
            return false;
        }

        events::ocean::GetWaterBodyDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("WaterBodyComponent");

        bool removeBody = false;
        bool isOpen = drawHeader(removeBody);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::WaterBodyComponentData bodyData = *dataOpt;
            if (drawSettings(bodyData))
            {
                events::ocean::SetWaterBodyDataCommand cmd;
                cmd.entity = handle;
                cmd.data = bodyData;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeBody)
        {
            events::ocean::RemoveWaterBodyComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool WaterBodyDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##WaterBodyHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Water Body");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveWaterBody", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool WaterBodyDrawer::drawSettings(services::WaterBodyComponentData& bodyData)
    {
        bool changed = false;

        if (ImGui::Checkbox("Active", &bodyData.isActive))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Inactive bodies neither render nor float anything, and stop suppressing the ocean.");

        const char* typeNames[] = {"Lake", "Pool"};
        int typeIndex = bodyData.type == 1u ? 1 : 0;
        if (ImGui::Combo("Type", &typeIndex, typeNames, 2))
        {
            bodyData.type = static_cast<uint32_t>(typeIndex);
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Authoring label only for now — both types behave identically.");

        if (ImGui::DragFloat("Water Height", &bodyData.waterHeight, 0.05f, -1000.0f, 1000.0f, "%.2f"))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Surface level as an OFFSET above the entity's transform Y.\n"
                              "Move the entity to place the body; nudge this for a precise level.");

        if (ImGui::DragFloat2("Half Extents", &bodyData.halfExtents.x, 0.25f, 0.0f, 5000.0f, "%.2f"))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Half size of the XZ box, in WORLD METRES around the entity position.\n"
                              "Entity scale is not applied — these numbers mean what they say.");

        if (ImGui::DragFloat("Depth", &bodyData.depth, 0.1f, 0.0f, 1000.0f, "%.2f"))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Metres of water below the surface — the body's floor.\n"
                              "Buoyancy, swimming and the underwater post-process all stop below it,\n"
                              "so a rooftop pool does not submerge the room underneath.");

        ImGui::Spacing();
        if (ImGui::Checkbox("Physics Enabled", &bodyData.physicsEnabled))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Off makes this a hole in the water: it still draws, but nothing floats in it\n"
                              "and the ocean underneath does NOT take over.");

        ImGui::Spacing();
        ImGui::TextDisabled("Waves");

        bool band0 = (bodyData.bandMask & 1u) != 0u;
        bool band1 = (bodyData.bandMask & 2u) != 0u;
        bool band2 = (bodyData.bandMask & 4u) != 0u;
        bool bandsChanged = false;
        bandsChanged |= ImGui::Checkbox("Swell", &band0);
        ImGui::SameLine();
        bandsChanged |= ImGui::Checkbox("Agitation", &band1);
        ImGui::SameLine();
        bandsChanged |= ImGui::Checkbox("Ripples", &band2);
        if (bandsChanged)
        {
            bodyData.bandMask = (band0 ? 1u : 0u) | (band1 ? 2u : 0u) | (band2 ? 4u : 0u);
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Ocean FFT bands to run on this body. All off (the default) is a mirror-flat pool.\n"
                              "Horizontal chop is suppressed on bodies so the surface never spills past the rim.");

        ImGui::Spacing();
        ImGui::TextDisabled("Shoaling and breaking shore waves never apply to a body —\n"
                            "they are driven by the ocean's shore-depth field.");
        ImGui::TextDisabled("Interactive ripples still apply, but need an ocean entity\n"
                            "in the scene to supply their settings.");

        return changed;
    }
}
