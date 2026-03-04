#include "DirectionalLightDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include <imgui.h>
#include <algorithm>

namespace windows::details {

    bool DirectionalLightDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasDirectionalLightComponentQuery hasLightQuery;
        hasLightQuery.entity = handle;
        bool hasLight = dispatcher.query(hasLightQuery);

        if (!hasLight)
            return false;

        events::scene::GetDirectionalLightDataQuery lightQuery;
        lightQuery.entity = handle;
        auto lightOpt = dispatcher.query(lightQuery);

        if (!lightOpt.has_value())
            return true;

        ImGui::PushID("DirectionalLightComponent");

        bool removeLight = false;

        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##DirectionalLightHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Directional Light");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveDirectionalLight", ImVec2(18, 18)))
        {
            removeLight = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::DirectionalLightData light = *lightOpt;
            bool changed = false;

            changed |= ImGui::ColorEdit3("Color", &light.color.x);
            changed |= ImGui::DragFloat("Intensity", &light.intensity, 0.01f, 0.0f, 100.0f);
            changed |= ImGui::Checkbox("Show Gizmo", &light.showGizmo);

            if (changed)
            {
                light.color = glm::clamp(light.color, glm::vec3(0.0f), glm::vec3(1.0f));
                light.intensity = std::max(0.0f, light.intensity);

                events::scene::SetDirectionalLightDataCommand cmd;
                cmd.entity = handle;
                cmd.lightData = light;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeLight)
        {
            events::scene::RemoveDirectionalLightComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

}
