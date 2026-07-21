#include "IBLDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include <glm/glm.hpp>
#include <imgui.h>

namespace windows::details {

    void IBLDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasIBLComponentQuery hasIBLQuery;
        hasIBLQuery.entity = handle;
        bool hasIBL = dispatcher.query(hasIBLQuery);

        if (!hasIBL)
            return;

        events::scene::GetIBLDataQuery iblQuery;
        iblQuery.entity = handle;
        auto iblOpt = dispatcher.query(iblQuery);

        if (!iblOpt.has_value())
            return;

        ImGui::PushID("IBLComponent");

        bool removeIBL = false;

        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##IBLHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("IBL");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveIBL", ImVec2(18, 18)))
        {
            removeIBL = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);
            ImGui::Text("File: %s", iblOpt->hdrRef.resolve().c_str());

            // VK-1574: live IBL knobs — an edit updates both the scene component and the
            // renderer (SetIBLParamsCommand) without re-baking the environment.
            float intensity = iblOpt->intensity;
            float rotationDeg = iblOpt->rotationDeg;
            float tint[3] = {iblOpt->tint.x, iblOpt->tint.y, iblOpt->tint.z};

            bool changed = false;
            changed |= ImGui::SliderFloat("Intensity", &intensity, 0.0f, 5.0f);
            changed |= ImGui::SliderFloat("Rotation", &rotationDeg, 0.0f, 360.0f);
            changed |= ImGui::ColorEdit3("Tint", tint);

            if (changed)
            {
                events::scene::SetIBLDataCommand dataCmd;
                dataCmd.entity = handle;
                dataCmd.iblData = *iblOpt; // keep hdrRef
                dataCmd.iblData.intensity = intensity;
                dataCmd.iblData.rotationDeg = rotationDeg;
                dataCmd.iblData.tint = glm::vec3(tint[0], tint[1], tint[2]);
                dispatcher.execute(dataCmd);

                events::render::SetIBLParamsCommand paramsCmd;
                paramsCmd.intensity = intensity;
                paramsCmd.rotationDeg = rotationDeg;
                paramsCmd.tint = glm::vec3(tint[0], tint[1], tint[2]);
                dispatcher.execute(paramsCmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeIBL)
        {
            events::scene::RemoveIBLComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);

            events::render::RemoveIBLCommand removeRenderCmd;
            dispatcher.execute(removeRenderCmd);
        }
    }

}
