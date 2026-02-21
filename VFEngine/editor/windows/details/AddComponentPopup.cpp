#include "AddComponentPopup.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/ScriptingEvents.hpp"
#include "events/UIEvents.hpp"
#include "events/SocketEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    void AddComponentPopup::draw(const ComponentPresence& c)
    {
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
            drawGeneralSection(c);
            drawPhysicsSection(c);
            drawAnimationSection(c);
            drawLightingSection(c);
            drawUISection(c);

            bool allAdded = c.hasCamera && c.hasMesh && c.hasAudio2D && c.hasAudio3D && c.hasScript &&
                           c.hasCollider && c.hasRigidBody && c.hasPhysicsAnimation &&
                           c.hasVFX && c.hasBillboard && c.hasText &&
                           c.hasDirectionalLight && c.hasPointLight && c.hasSpotLight &&
                           c.hasSocketAttachment && c.hasUICanvas &&
                           c.hasUIRect && c.hasUIImage && c.hasUILabel && c.hasUIScroll && c.hasUILayoutGroup &&
                           c.hasUIButton && c.hasUITextInput && c.hasUICheckbox && c.hasUIDropdown &&
                           c.hasUITabs && c.hasUISlider && c.hasUIProgressBar &&
                           c.hasNavmeshAgent;
            if (allAdded)
            {
                ImGui::Spacing();
                ImGui::TextDisabled("All components added");
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleVar(2);
    }

    void AddComponentPopup::drawGeneralSection(const ComponentPresence& c)
    {
        auto handle = c.handle;
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::TextDisabled("Components");
        ImGui::Separator();

        if (!c.hasCamera)
        {
            if (ImGui::Selectable("  Camera"))
            {
                events::scene::AddCameraComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
        }

        if (!c.hasMesh)
        {
            if (ImGui::Selectable("  Mesh"))
            {
                events::scene::AddMeshComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
        }

        if (!c.hasAudio2D)
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

        if (!c.hasAudio3D)
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

        if (!c.hasScript)
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

        if (!c.hasVFX)
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

        if (!c.hasBillboard)
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

        if (!c.hasText)
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
    }

    void AddComponentPopup::drawPhysicsSection(const ComponentPresence& c)
    {
        auto handle = c.handle;
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::Spacing();
        ImGui::TextDisabled("Physics");
        ImGui::Separator();

        if (!c.hasCollider)
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

        if (!c.hasRigidBody)
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

        if (!c.hasPhysicsAnimation)
        {
            if (ImGui::Selectable("  Physics Animation"))
            {
                events::scene::AddPhysicsAnimationComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Ragdoll and kinematic bone physics for animated meshes");
            }
        }

        if (!c.hasNavmeshAgent)
        {
            if (ImGui::Selectable("  Navmesh Agent"))
            {
                events::scene::AddNavmeshAgentComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Navigation mesh agent for pathfinding and crowd movement");
            }
        }
    }

    void AddComponentPopup::drawAnimationSection(const ComponentPresence& c)
    {
        auto handle = c.handle;
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::Spacing();
        ImGui::TextDisabled("Animation");
        ImGui::Separator();

        if (!c.hasSocketAttachment)
        {
            if (ImGui::Selectable("  Socket Attachment"))
            {
                events::socket::AddSocketAttachmentComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Attach this entity to an animation socket on a parent skeleton");
            }
        }
    }

    void AddComponentPopup::drawLightingSection(const ComponentPresence& c)
    {
        auto handle = c.handle;
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::Spacing();
        ImGui::TextDisabled("Lighting");
        ImGui::Separator();

        if (!c.hasDirectionalLight)
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

        if (!c.hasPointLight)
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

        if (!c.hasSpotLight)
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
    }

    void AddComponentPopup::drawUISection(const ComponentPresence& c)
    {
        auto handle = c.handle;
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::Spacing();
        ImGui::TextDisabled("UI");
        ImGui::Separator();

        if (!c.hasUICanvas)
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

        if (!c.hasUIRect)
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

        if (!c.hasUIImage)
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

        if (!c.hasUILabel)
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

        if (!c.hasUIScroll)
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

        if (!c.hasUILayoutGroup)
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

        if (!c.hasUIButton)
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

        if (!c.hasUITextInput)
        {
            if (ImGui::Selectable("  UI Text Input"))
            {
                events::ui::AddUITextInputComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Editable text input field with focus and selection");
            }
        }

        if (!c.hasUICheckbox)
        {
            if (ImGui::Selectable("  UI Checkbox"))
            {
                events::ui::AddUICheckboxComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Toggleable checkbox with radio group support");
            }
        }

        if (!c.hasUIDropdown)
        {
            if (ImGui::Selectable("  UI Dropdown"))
            {
                events::ui::AddUIDropdownComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Dropdown / combo box with selectable options");
            }
        }

        if (!c.hasUITabs)
        {
            if (ImGui::Selectable("  UI Tabs"))
            {
                events::ui::AddUITabsComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Tabbed panel container with switchable content views");
            }
        }

        if (!c.hasUISlider)
        {
            if (ImGui::Selectable("  UI Slider"))
            {
                events::ui::AddUISliderComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Draggable slider for numeric value input");
            }
        }

        if (!c.hasUIProgressBar)
        {
            if (ImGui::Selectable("  UI Progress Bar"))
            {
                events::ui::AddUIProgressBarComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Non-interactive bar displaying progress");
            }
        }
    }
}
