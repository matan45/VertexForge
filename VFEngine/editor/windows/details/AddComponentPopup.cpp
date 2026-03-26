#include "AddComponentPopup.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "events/scripting/ScriptingEvents.hpp"
#include "events/physics/SocketEvents.hpp"
#include "events/physics/IKEvents.hpp"
#include "events/scene/ReverbZoneEvents.hpp"
#include "events/scene/FogVolumeEvents.hpp"
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
            drawPluginSection(c);

            bool allAdded = c.hasCamera && c.hasMesh && c.hasAudio2D && c.hasAudio3D && c.hasScript &&
                           c.hasCollider && c.hasRigidBody && c.hasPhysicsAnimation &&
                           c.hasVFX && c.hasBillboard && c.hasText &&
                           c.hasDirectionalLight && c.hasPointLight && c.hasSpotLight &&
                           c.hasSocketAttachment && c.hasUICanvas &&
                           c.hasUIRect && c.hasUIImage && c.hasUILabel && c.hasUIScroll && c.hasUILayoutGroup &&
                           c.hasUIButton && c.hasUITextInput && c.hasUICheckbox && c.hasUIDropdown &&
                           c.hasUITabs && c.hasUISlider && c.hasUIProgressBar && c.hasUIAnimation && c.hasUIMask &&
                           c.hasUIDraggable && c.hasUIDropTarget &&
                           c.hasNavmeshAgent && c.hasOffMeshLink && c.hasNavmeshObstacle && c.hasNavmeshModifierVolume && c.hasRenderTexture && c.hasController &&
                           c.hasIK && c.hasBehaviorTree && c.hasDecal && c.hasReverbZone &&
                           c.hasFogVolume;
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
                ImGui::SetTooltip("Streaming audio for background music and ambient sounds");
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
                ImGui::SetTooltip("Cached audio for spatial sound effects");
        }

        if (!c.hasReverbZone)
        {
            if (ImGui::Selectable("  Reverb Zone"))
            {
                events::scene::AddReverbZoneComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Environmental reverb zone with shape volume and presets");
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
                ImGui::SetTooltip("mType script for custom behavior");
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
                ImGui::SetTooltip("Visual effects particle system");
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
                ImGui::SetTooltip("Camera-facing textured quad");
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
                ImGui::SetTooltip("SDF text rendering with custom fonts");
        }

        if (!c.hasRenderTexture)
        {
            if (ImGui::Selectable("  Render Texture"))
            {
                events::scene::AddRenderTextureComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Renders camera view to a texture (requires Camera component)");
        }

        if (!c.hasDecal)
        {
            if (ImGui::Selectable("  Decal"))
            {
                events::scene::AddDecalComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Projects decal onto scene geometry (footprints, bullet holes, etc.)");
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
                ImGui::SetTooltip("Collision shape (Box, Sphere, Capsule, Convex Mesh, or Triangle Mesh)");
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
                ImGui::SetTooltip("Physics body for dynamics simulation");
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
                ImGui::SetTooltip("Ragdoll and kinematic bone physics for animated meshes");
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
                ImGui::SetTooltip("Navigation mesh agent for pathfinding and crowd movement");
        }

        if (!c.hasOffMeshLink)
        {
            if (ImGui::Selectable("  Off-Mesh Link"))
            {
                events::scene::AddOffMeshLinkComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Off-mesh connection for jumps, climbs, and drops between navmesh regions");
        }

        if (!c.hasNavmeshObstacle)
        {
            if (ImGui::Selectable("  Navmesh Obstacle"))
            {
                events::scene::AddNavmeshObstacleComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Dynamic obstacle that carves holes in navmesh or triggers agent avoidance");
        }

        if (!c.hasNavmeshModifierVolume)
        {
            if (ImGui::Selectable("  Navmesh Modifier Volume"))
            {
                events::scene::AddNavmeshModifierVolumeComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Volume that overrides navmesh area type (Road, Mud, Water, etc.)");
        }

        if (!c.hasController)
        {
            if (ImGui::Selectable("  Controller"))
            {
                events::scene::AddControllerComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Player or AI controller for character movement and input");
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
                ImGui::SetTooltip("Attach this entity to an animation socket on a parent skeleton");
        }

        if (!c.hasIK)
        {
            if (ImGui::Selectable("  Inverse Kinematics"))
            {
                events::ik::AddIKComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("FABRIK IK solver for bone chain targeting (feet, hands, look-at)");
        }

        if (!c.hasBehaviorTree)
        {
            if (ImGui::Selectable("  Behavior Tree"))
            {
                events::scene::AddBehaviorTreeComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("AI behavior tree for complex decision-making");
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
                ImGui::SetTooltip("Infinite distance light (sun, moon)");
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
                ImGui::SetTooltip("Omnidirectional light with range attenuation");
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
                ImGui::SetTooltip("Cone-shaped light with inner/outer angles");
        }

        if (!c.hasFogVolume)
        {
            if (ImGui::Selectable("  Fog Volume"))
            {
                events::scene::AddFogVolumeComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Localized fog region (Box, Sphere, or Cylinder) with density control");
        }
    }
}
