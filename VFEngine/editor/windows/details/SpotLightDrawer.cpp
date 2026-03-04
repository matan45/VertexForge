#include "SpotLightDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include <imgui.h>
#include <algorithm>

namespace windows::details {

    bool SpotLightDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasSpotLightComponentQuery hasLightQuery;
        hasLightQuery.entity = handle;
        bool hasLight = dispatcher.query(hasLightQuery);

        if (!hasLight)
            return false;

        events::scene::GetSpotLightDataQuery lightQuery;
        lightQuery.entity = handle;
        auto lightOpt = dispatcher.query(lightQuery);

        if (!lightOpt.has_value())
            return true;

        ImGui::PushID("SpotLightComponent");

        bool removeLight = false;

        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##SpotLightHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Spot Light");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveSpotLight", ImVec2(18, 18)))
        {
            removeLight = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::SpotLightData light = *lightOpt;
            bool changed = false;

            changed |= ImGui::ColorEdit3("Color", &light.color.x);
            changed |= ImGui::DragFloat("Intensity", &light.intensity, 0.01f, 0.0f, 100.0f);
            changed |= ImGui::DragFloat("Inner Angle", &light.innerAngle, 0.5f, 0.0f, 89.0f);
            changed |= ImGui::DragFloat("Outer Angle", &light.outerAngle, 0.5f, 1.0f, 90.0f);
            changed |= ImGui::DragFloat("Range", &light.range, 0.1f, 0.1f, 1000.0f);
            changed |= ImGui::Checkbox("Show Gizmo", &light.showGizmo);

            if (changed)
            {
                light.color = glm::clamp(light.color, glm::vec3(0.0f), glm::vec3(1.0f));
                light.intensity = std::max(0.0f, light.intensity);
                light.range = std::max(0.1f, light.range);

                light.innerAngle = std::clamp(light.innerAngle, 0.0f, 89.0f);
                light.outerAngle = std::clamp(light.outerAngle, 1.0f, 90.0f);

                if (light.innerAngle >= light.outerAngle)
                {
                    light.outerAngle = std::min(light.innerAngle + 1.0f, 90.0f);
                    if (light.innerAngle >= light.outerAngle)
                    {
                        light.innerAngle = light.outerAngle - 1.0f;
                    }
                }

                events::scene::SetSpotLightDataCommand cmd;
                cmd.entity = handle;
                cmd.lightData = light;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeLight)
        {
            events::scene::RemoveSpotLightComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

}
