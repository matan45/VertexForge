#include "PointLightDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include <imgui.h>
#include <algorithm>

namespace windows::details {

    void PointLightDrawer::drawShadowOverride(services::EntityHandle entity,
                                               events::EventDispatcher& dispatcher)
    {
        ImGui::Indent(10.0f);

        events::scene::HasShadowOverrideQuery hasQuery;
        hasQuery.entity = entity;
        bool hasOverride = dispatcher.query(hasQuery);

        if (!hasOverride)
        {
            if (ImGui::Button("Add Shadow Override##point"))
            {
                events::scene::SetShadowOverrideDataCommand cmd;
                cmd.entity = entity;
                cmd.data = {};
                dispatcher.execute(cmd);
            }
        }
        else
        {
            if (ImGui::Button("Remove Shadow Override##point"))
            {
                events::scene::RemoveShadowOverrideCommand cmd;
                cmd.entity = entity;
                dispatcher.execute(cmd);
            }
            else
            {
                events::scene::GetShadowOverrideDataQuery getQuery;
                getQuery.entity = entity;
                auto overrideOpt = dispatcher.query(getQuery);
                if (overrideOpt.has_value())
                {
                    auto override = *overrideOpt;
                    bool overrideChanged = false;

                    overrideChanged |= ImGui::DragFloat("Depth Bias##shadow", &override.depthBias, 0.0001f, -1.0f, 0.1f);
                    ImGui::SameLine();
                    ImGui::TextDisabled("(-1 = global)");

                    overrideChanged |= ImGui::DragFloat("Slope Bias##shadow", &override.slopeBias, 0.01f, -1.0f, 5.0f);
                    overrideChanged |= ImGui::DragFloat("Normal Bias##shadow", &override.normalBias, 0.001f, -1.0f, 1.0f);

                    int maxPages = static_cast<int>(override.maxPages);
                    if (ImGui::SliderInt("Max Pages##shadow", &maxPages, 0, 16))
                    {
                        override.maxPages = static_cast<uint32_t>(maxPages);
                        overrideChanged = true;
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("(0 = default)");

                    if (overrideChanged)
                    {
                        events::scene::SetShadowOverrideDataCommand cmd;
                        cmd.entity = entity;
                        cmd.data = override;
                        dispatcher.execute(cmd);
                    }
                }
            }
        }

        ImGui::Unindent(10.0f);
    }

    bool PointLightDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasPointLightComponentQuery hasLightQuery;
        hasLightQuery.entity = handle;
        bool hasLight = dispatcher.query(hasLightQuery);

        if (!hasLight)
            return false;

        events::scene::GetPointLightDataQuery lightQuery;
        lightQuery.entity = handle;
        auto lightOpt = dispatcher.query(lightQuery);

        if (!lightOpt.has_value())
            return true;

        ImGui::PushID("PointLightComponent");

        bool removeLight = false;

        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##PointLightHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Point Light");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemovePointLight", ImVec2(18, 18)))
        {
            removeLight = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::PointLightData light = *lightOpt;
            bool changed = false;

            changed |= ImGui::ColorEdit3("Color", &light.color.x);
            changed |= ImGui::DragFloat("Intensity", &light.intensity, 0.01f, 0.0f, 100.0f);
            changed |= ImGui::DragFloat("Radius", &light.radius, 0.1f, 0.1f, 1000.0f);
            changed |= ImGui::DragFloat("Light Size", &light.lightSize, 0.01f, 0.01f, 10.0f);
            changed |= ImGui::Checkbox("Cast Shadows", &light.castsShadow);
            changed |= ImGui::Checkbox("Show Gizmo", &light.showGizmo);

            if (changed)
            {
                light.color = glm::clamp(light.color, glm::vec3(0.0f), glm::vec3(1.0f));
                light.intensity = std::max(0.0f, light.intensity);
                light.radius = std::max(0.1f, light.radius);

                events::scene::SetPointLightDataCommand cmd;
                cmd.entity = handle;
                cmd.lightData = light;
                dispatcher.execute(cmd);
            }

            if (light.castsShadow)
            {
                drawShadowOverride(handle, dispatcher);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeLight)
        {
            events::scene::RemovePointLightComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

}
