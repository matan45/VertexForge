#include "WaterWakeEmitterDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool WaterWakeEmitterDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasWaterWakeEmitterComponentQuery hasQuery;
        hasQuery.entity = handle;
        if (!dispatcher.query(hasQuery))
        {
            return false;
        }

        events::scene::GetWaterWakeEmitterDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("WaterWakeEmitterComponent");

        bool removeEmitter = false;
        bool isOpen = drawHeader(removeEmitter);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::WaterWakeEmitterComponentData emitterData = *dataOpt;
            if (drawSettings(emitterData))
            {
                events::scene::SetWaterWakeEmitterDataCommand cmd;
                cmd.entity = handle;
                cmd.emitterData = emitterData;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeEmitter)
        {
            events::scene::RemoveWaterWakeEmitterComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool WaterWakeEmitterDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##WaterWakeEmitterHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Water Wake Emitter");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveWaterWakeEmitter", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool WaterWakeEmitterDrawer::drawSettings(services::WaterWakeEmitterComponentData& emitterData)
    {
        bool changed = false;

        if (ImGui::Checkbox("Enabled", &emitterData.enabled))
            changed = true;

        if (ImGui::DragFloat3("Offset", &emitterData.offset.x, 0.01f, -100.0f, 100.0f, "%.2f"))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Local-space offset from the entity origin — put it on the bow, not the hull centre");

        if (ImGui::DragFloat("Radius", &emitterData.radius, 0.05f, 0.05f, 25.0f, "%.2f"))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Metres of surface each impulse covers. The ripple is exactly zero outside it.");

        if (ImGui::DragFloat("Strength", &emitterData.strength, 0.01f, -10.0f, 10.0f, "%.2f"))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Vertical velocity kick at the centre.\nNegative pushes the surface DOWN, which is what a hull does.");

        if (ImGui::DragFloat("Min Speed", &emitterData.minSpeed, 0.05f, 0.0f, 50.0f, "%.2f"))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("m/s below which the emitter stays quiet");

        if (ImGui::Checkbox("Continuous", &emitterData.continuous))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Emit every tick above Min Speed instead of every Travel Interval metres");

        if (!emitterData.continuous)
        {
            if (ImGui::DragFloat("Travel Interval", &emitterData.travelInterval, 0.05f, 0.05f, 20.0f, "%.2f"))
                changed = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Metres of travel between impulses. Distance-based, so ring spacing\n"
                                  "does not change with speed the way a time interval would.");
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Requires the ocean's Interactive Ripples to be enabled.");

        return changed;
    }
}
