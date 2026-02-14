#include "AddComponentPopup.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "events/ScriptingEvents.hpp"
#include "events/UIEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    void AddComponentPopup::draw(services::EntityHandle handle, bool hasCamera, bool hasMesh,
                                 bool hasAudio2D, bool hasAudio3D, bool hasScript,
                                 bool hasCollider, bool hasRigidBody, bool hasVFX, bool hasBillboard,
                                 bool hasText, bool hasDirectionalLight, bool hasPointLight, bool hasSpotLight,
                                 bool hasUICanvas, bool hasUIRect, bool hasUIImage, bool hasUILabel,
                                 bool hasUIScroll, bool hasUILayoutGroup, bool hasUIButton)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float buttonWidth = ImGui::GetContentRegionAvail().x * 0.6f;
        float buttonOffset = (ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + buttonOffset);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

        if (ImGui::Button("Add Component", ImVec2(buttonWidth, 28)))
        {
            ImGui::OpenPopup("AddComponentPopup");
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));

        if (ImGui::BeginPopup("AddComponentPopup"))
        {
            ImGui::TextDisabled("Components");
            ImGui::Separator();

            if (!hasCamera)
            {
                if (ImGui::Selectable("  Camera"))
                {
                    events::scene::AddCameraComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
            }

            if (!hasMesh)
            {
                if (ImGui::Selectable("  Mesh"))
                {
                    events::scene::AddMeshComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
            }

            if (!hasAudio2D)
            {
                if (ImGui::Selectable("  Audio Source 2D"))
                {
                    events::scene::AddAudioSource2DComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Streaming audio for background music and ambient sounds");
                }
            }

            if (!hasAudio3D)
            {
                if (ImGui::Selectable("  Audio Source 3D"))
                {
                    events::scene::AddAudioSource3DComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Cached audio for spatial sound effects");
                }
            }


            if (!hasScript)
            {
                if (ImGui::Selectable("  Script"))
                {
                    events::scripting::AttachScriptCommand cmd;
                    cmd.entity = handle;
                    cmd.data.scriptPath = "";
                    cmd.data.enabled = true;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("mType script for custom behavior");
                }
            }

            if (!hasVFX)
            {
                if (ImGui::Selectable("  VFX"))
                {
                    events::scene::AddVFXComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Visual effects particle system");
                }
            }

            if (!hasBillboard)
            {
                if (ImGui::Selectable("  Billboard"))
                {
                    events::scene::AddBillboardComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Camera-facing textured quad");
                }
            }

            if (!hasText)
            {
                if (ImGui::Selectable("  Text"))
                {
                    events::scene::AddTextComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("SDF text rendering with custom fonts");
                }
            }

            // Physics components section
            ImGui::Spacing();
            ImGui::TextDisabled("Physics");
            ImGui::Separator();

            if (!hasCollider)
            {
                if (ImGui::Selectable("  Collider"))
                {
                    events::scene::AddColliderComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Collision shape (Box, Sphere, Capsule, Convex Mesh, or Triangle Mesh)");
                }
            }

            if (!hasRigidBody)
            {
                if (ImGui::Selectable("  Rigid Body"))
                {
                    events::scene::AddRigidBodyComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Physics body for dynamics simulation");
                }
            }

            // Lighting components section
            ImGui::Spacing();
            ImGui::TextDisabled("Lighting");
            ImGui::Separator();

            if (!hasDirectionalLight)
            {
                if (ImGui::Selectable("  Directional Light"))
                {
                    events::scene::AddDirectionalLightComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Infinite distance light (sun, moon)");
                }
            }

            if (!hasPointLight)
            {
                if (ImGui::Selectable("  Point Light"))
                {
                    events::scene::AddPointLightComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Omnidirectional light with range attenuation");
                }
            }

            if (!hasSpotLight)
            {
                if (ImGui::Selectable("  Spot Light"))
                {
                    events::scene::AddSpotLightComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Cone-shaped light with inner/outer angles");
                }
            }

            // UI components section
            ImGui::Spacing();
            ImGui::TextDisabled("UI");
            ImGui::Separator();

            if (!hasUICanvas)
            {
                if (ImGui::Selectable("  UI Canvas"))
                {
                    events::ui::AddUICanvasComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("UI Canvas with reference resolution and auto-scaling");
                }
            }

            if (!hasUIRect)
            {
                if (ImGui::Selectable("  UI Rect"))
                {
                    events::ui::AddUIRectComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Rect transform for UI anchoring and layout");
                }
            }

            if (!hasUIImage)
            {
                if (ImGui::Selectable("  UI Image"))
                {
                    events::ui::AddUIImageComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Screen-space image with texture and color tint");
                }
            }

            if (!hasUILabel)
            {
                if (ImGui::Selectable("  UI Label"))
                {
                    events::ui::AddUILabelComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Text label with font, alignment, and overflow settings");
                }
            }

            if (!hasUIScroll)
            {
                if (ImGui::Selectable("  UI Scroll"))
                {
                    events::ui::AddUIScrollComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Scrollable container with clipping and scrollbars");
                }
            }

            if (!hasUILayoutGroup)
            {
                if (ImGui::Selectable("  UI Layout Group"))
                {
                    events::ui::AddUILayoutGroupComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Auto-stack children vertically or horizontally");
                }
            }

            if (!hasUIButton)
            {
                if (ImGui::Selectable("  UI Button"))
                {
                    events::ui::AddUIButtonComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Interactive button with state colors and click events");
                }
            }

            bool allAdded = hasCamera && hasMesh && hasAudio2D && hasAudio3D && hasScript &&
                           hasCollider && hasRigidBody && hasVFX && hasBillboard && hasText &&
                           hasDirectionalLight && hasPointLight && hasSpotLight && hasUICanvas &&
                           hasUIRect && hasUIImage && hasUILabel && hasUIScroll && hasUILayoutGroup &&
                           hasUIButton;
            if (allAdded)
            {
                ImGui::Spacing();
                ImGui::TextDisabled("All components added");
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleVar(2);
    }
}
