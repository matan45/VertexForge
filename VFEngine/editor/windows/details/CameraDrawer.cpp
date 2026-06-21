#include "CameraDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include <imgui.h>
#include <cstdint>
#include <cstdio>

namespace windows::details {

    bool CameraDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasCameraComponentQuery hasCameraQuery;
        hasCameraQuery.entity = handle;
        bool hasCamera = dispatcher.query(hasCameraQuery);

        if (!hasCamera)
            return false;

        events::scene::GetCameraDataQuery cameraQuery;
        cameraQuery.entity = handle;
        auto cameraOpt = dispatcher.query(cameraQuery);

        if (!cameraOpt.has_value())
            return true;

        ImGui::PushID("CameraComponent");

        bool removeCamera = false;

        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##CameraHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Camera");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveCamera", ImVec2(18, 18)))
        {
            removeCamera = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::CameraData camera = *cameraOpt;
            bool changed = false;

            changed |= ImGui::DragFloat("Field of View", &camera.fieldOfView, 1.0f, 1.0, 180.0f);
            changed |= ImGui::DragFloat("Near Plane", &camera.nearPlane, 0.01f, 0.01f, camera.farPlane - 0.1f);
            changed |= ImGui::DragFloat("Far Plane", &camera.farPlane, 0.1f, camera.nearPlane + 0.1f, 10000.0f);
            changed |= ImGui::DragFloat("Aspect Ratio", &camera.aspectRatio, 0.01f, 0.1f, 10.0f);
            changed |= ImGui::Checkbox("Perspective", &camera.isPerspective);
            changed |= ImGui::Checkbox("Primary Camera", &camera.isPrimary);
            changed |= ImGui::Checkbox("Show Frustum", &camera.showFrustum);

            if (!camera.isPerspective)
            {
                changed |= ImGui::DragFloat("Orthographic Size", &camera.orthoSize, 0.1f, 0.1f, 1000.0f);
            }

            // VK-1415: per-camera render-layer culling mask.
            ImGui::Separator();
            ImGui::Text("Culling Mask");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Render layers this camera draws (VK-1415).\n"
                                  "A mesh on layer N is visible only if bit N is set here.\n"
                                  "Applies to RTT / secondary cameras (e.g. a minimap or security cam).\n"
                                  "The main viewport always renders all layers.");
            }
            ImGui::SameLine();
            if (camera.cullingMask == 0xFFFFFFFFu)
                ImGui::TextDisabled("(All layers)");
            else
                ImGui::TextDisabled("(0x%08X)", camera.cullingMask);

            {
                uint32_t mask = camera.cullingMask;
                if (ImGui::InputScalar("Mask (hex)", ImGuiDataType_U32, &mask, nullptr, nullptr, "%08X",
                                       ImGuiInputTextFlags_CharsHexadecimal))
                {
                    camera.cullingMask = mask;
                    changed = true;
                }
            }
            if (ImGui::TreeNode("Layers"))
            {
                for (int layer = 0; layer < 32; ++layer)
                {
                    bool on = (camera.cullingMask & (1u << layer)) != 0u;
                    char label[24];
                    std::snprintf(label, sizeof(label), "Layer %d", layer);
                    if (ImGui::Checkbox(label, &on))
                    {
                        if (on)
                            camera.cullingMask |= (1u << layer);
                        else
                            camera.cullingMask &= ~(1u << layer);
                        changed = true;
                    }
                }
                ImGui::TreePop();
            }

            if (changed)
            {
                events::scene::SetCameraDataCommand cmd;
                cmd.entity = handle;
                cmd.cameraData = camera;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeCamera)
        {
            events::scene::RemoveCameraComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

}
