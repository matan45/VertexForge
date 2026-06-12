#include "BuoyancyDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool BuoyancyDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasBuoyancyComponentQuery hasBuoyancyQuery;
        hasBuoyancyQuery.entity = handle;
        if (!dispatcher.query(hasBuoyancyQuery))
        {
            return false;
        }

        events::scene::GetBuoyancyDataQuery buoyancyQuery;
        buoyancyQuery.entity = handle;
        auto buoyancyOpt = dispatcher.query(buoyancyQuery);

        if (!buoyancyOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("BuoyancyComponent");

        bool removeBuoyancy = false;
        bool isOpen = drawHeader(removeBuoyancy);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::BuoyancyComponentData buoyancyData = *buoyancyOpt;
            bool changed = false;

            changed |= drawSettings(buoyancyData);
            if (buoyancyData.customSampleMode)
            {
                ImGui::Spacing();
                changed |= drawCustomPoints(buoyancyData);
            }

            if (changed)
            {
                events::scene::SetBuoyancyDataCommand cmd;
                cmd.entity = handle;
                cmd.buoyancyData = buoyancyData;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeBuoyancy)
        {
            events::scene::RemoveBuoyancyComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool BuoyancyDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##BuoyancyHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Buoyancy");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveBuoyancy", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool BuoyancyDrawer::drawSettings(services::BuoyancyComponentData& buoyancyData)
    {
        bool changed = false;

        if (ImGui::Checkbox("Custom Sample Points", &buoyancyData.customSampleMode))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Off: hull points derived from the collider shape.\n"
                              "On: author explicit local-space sample points (e.g. boat hull corners).");
        }

        if (ImGui::DragFloat("Buoyancy Scale", &buoyancyData.buoyancyScale, 0.01f, 0.0f, 10.0f, "%.2f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Multiplier on the ocean's buoyancy strength for this entity");
        }

        if (ImGui::DragFloat("Angular Drag", &buoyancyData.angularDrag, 0.01f, 0.0f, 5.0f, "%.2f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Rotational damping while submerged (settles bobbing/rolling)");
        }

        return changed;
    }

    bool BuoyancyDrawer::drawCustomPoints(services::BuoyancyComponentData& buoyancyData)
    {
        bool changed = false;

        ImGui::Text("Sample Points:");
        ImGui::Indent(10.0f);

        int pointCount = static_cast<int>(buoyancyData.customPointCount);
        if (ImGui::SliderInt("Count", &pointCount, 1, 8))
        {
            buoyancyData.customPointCount = static_cast<uint32_t>(pointCount);
            changed = true;
        }

        for (uint32_t i = 0; i < buoyancyData.customPointCount && i < 8; ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            float point[3] = {buoyancyData.customPoints[i].x,
                              buoyancyData.customPoints[i].y,
                              buoyancyData.customPoints[i].z};
            if (ImGui::DragFloat3("##Point", point, 0.05f))
            {
                buoyancyData.customPoints[i] = glm::vec3(point[0], point[1], point[2]);
                changed = true;
            }
            ImGui::PopID();
        }

        ImGui::Unindent(10.0f);

        return changed;
    }
}
