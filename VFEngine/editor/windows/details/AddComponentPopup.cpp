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
#include "events/scene/ReflectionProbeEvents.hpp"
#include <imgui.h>
#include <cctype>
#include <cstring>

namespace windows::details
{
    bool AddComponentPopup::matchesFilter(const char* label, const char* filter)
    {
        if (!filter || filter[0] == '\0') return true;
        size_t needleLen = std::strlen(filter);
        size_t hayLen = std::strlen(label);
        if (needleLen > hayLen) return false;
        for (size_t i = 0; i <= hayLen - needleLen; ++i)
        {
            bool match = true;
            for (size_t j = 0; j < needleLen; ++j)
            {
                if (std::tolower(static_cast<unsigned char>(label[i + j])) !=
                    std::tolower(static_cast<unsigned char>(filter[j])))
                {
                    match = false;
                    break;
                }
            }
            if (match) return true;
        }
        return false;
    }

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
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputTextWithHint("##CompSearch", "Search...", searchBuffer, sizeof(searchBuffer));
            ImGui::Separator();

            const char* filter = searchBuffer[0] != '\0' ? searchBuffer : nullptr;

            if (filter)
            {
                drawGeneralSection(c, filter);
                drawPhysicsSection(c, filter);
                drawAnimationSection(c, filter);
                drawLightingSection(c, filter);
                drawUISection(c, filter);
                drawPluginSection(c, filter);

            }
            else
            {
                if (ImGui::BeginMenu("Components"))
                {
                    drawGeneralSection(c);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Physics"))
                {
                    drawPhysicsSection(c);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Animation"))
                {
                    drawAnimationSection(c);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Lighting"))
                {
                    drawLightingSection(c);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("UI"))
                {
                    drawUISection(c);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Plugins"))
                {
                    drawPluginSection(c);
                    ImGui::EndMenu();
                }
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleVar(2);
    }

    void AddComponentPopup::drawGeneralSection(const ComponentPresence& c, const char* filter)
    {
        auto handle = c.handle;
        auto& dispatcher = events::EventDispatcher::instance();

        if (!c.hasCamera && matchesFilter("Camera", filter))
        {
            if (ImGui::Selectable("  Camera"))
            {
                events::scene::AddCameraComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
        }

        if (!c.hasMesh && matchesFilter("Mesh", filter))
        {
            if (ImGui::Selectable("  Mesh"))
            {
                events::scene::AddMeshComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
        }

        if (!c.hasAudio2D && matchesFilter("Audio Source 2D", filter))
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

        if (!c.hasAudio3D && matchesFilter("Audio Source 3D", filter))
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

        if (!c.hasReverbZone && matchesFilter("Reverb Zone", filter))
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

        if (!c.hasScript && matchesFilter("Script", filter))
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

        if (!c.hasVFX && matchesFilter("VFX", filter))
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

        if (!c.hasVFXSequence && matchesFilter("VFX Sequence", filter))
        {
            if (ImGui::Selectable("  VFX Sequence"))
            {
                events::scene::AddVFXSequenceComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("VFX combo sequence (.vfVFXSequence) with event triggers");
        }

        if (!c.hasBillboard && matchesFilter("Billboard", filter))
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

        if (!c.hasText && matchesFilter("Text", filter))
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

        if (!c.hasRenderTexture && matchesFilter("Render Texture", filter))
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

        if (!c.hasDecal && matchesFilter("Decal", filter))
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

    void AddComponentPopup::drawPhysicsSection(const ComponentPresence& c, const char* filter)
    {
        auto handle = c.handle;
        auto& dispatcher = events::EventDispatcher::instance();

        if (!c.hasCollider && matchesFilter("Collider", filter))
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

        if (!c.hasRigidBody && matchesFilter("Rigid Body", filter))
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

        if (!c.hasVehicle && matchesFilter("Vehicle", filter))
        {
            if (ImGui::Selectable("  Vehicle"))
            {
                events::scene::AddVehicleComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Jolt vehicle constraint driven by a rigid-body chassis");
        }

        if (!c.hasBuoyancy && matchesFilter("Buoyancy", filter))
        {
            if (ImGui::Selectable("  Buoyancy"))
            {
                events::scene::AddBuoyancyComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Tunes ocean buoyancy hull sampling (boats: custom hull points + angular drag)");
        }

        if (!c.hasDestructible && matchesFilter("Destructible", filter))
        {
            if (ImGui::Selectable("  Destructible"))
            {
                events::scene::AddDestructibleComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Makes object destructible with health, fracture, and debris");
        }

        if (!c.hasPhysicsAnimation && matchesFilter("Physics Animation", filter))
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

        if (!c.hasNavmeshAgent && matchesFilter("Navmesh Agent", filter))
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

        if (!c.hasOffMeshLink && matchesFilter("Off-Mesh Link", filter))
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

        if (!c.hasNavmeshObstacle && matchesFilter("Navmesh Obstacle", filter))
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

        if (!c.hasNavmeshModifierVolume && matchesFilter("Navmesh Modifier Volume", filter))
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

        if (!c.hasNavInvoker && matchesFilter("Nav Invoker", filter))
        {
            if (ImGui::Selectable("  Nav Invoker"))
            {
                events::scene::AddNavInvokerComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Streaming source that generates navmesh tiles around this entity");
        }

        if (!c.hasVolumetricNavVolume && matchesFilter("Volumetric Nav Volume", filter))
        {
            if (ImGui::Selectable("  Volumetric Nav Volume"))
            {
                events::scene::AddVolumetricNavVolumeComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("3D volumetric navigation volume for flying/swimming pathfinding");
        }

        if (!c.hasVolumetricAgent && matchesFilter("Volumetric Agent", filter))
        {
            if (ImGui::Selectable("  Volumetric Agent"))
            {
                events::scene::AddVolumetricAgentComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("3D volumetric navigation agent for flying/swimming movement");
        }

        if (!c.hasController && matchesFilter("Controller", filter))
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

    void AddComponentPopup::drawAnimationSection(const ComponentPresence& c, const char* filter)
    {
        auto handle = c.handle;
        auto& dispatcher = events::EventDispatcher::instance();

        if (!c.hasSocketAttachment && matchesFilter("Socket Attachment", filter))
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

        if (!c.hasIK && matchesFilter("Inverse Kinematics", filter))
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

        if (!c.hasBehaviorTree && matchesFilter("Behavior Tree", filter))
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

    void AddComponentPopup::drawLightingSection(const ComponentPresence& c, const char* filter)
    {
        auto handle = c.handle;
        auto& dispatcher = events::EventDispatcher::instance();

        if (!c.hasDirectionalLight && matchesFilter("Directional Light", filter))
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

        if (!c.hasPointLight && matchesFilter("Point Light", filter))
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

        if (!c.hasSpotLight && matchesFilter("Spot Light", filter))
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

        if (!c.hasFogVolume && matchesFilter("Fog Volume", filter))
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

        if (!c.hasReflectionProbe && matchesFilter("Reflection Probe", filter))
        {
            if (ImGui::Selectable("  Reflection Probe"))
            {
                events::scene::AddReflectionProbeComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Local environment capture; overrides IBL specular reflections\n"
                                  "inside its bounds so interiors stop mirroring the sky");
        }
    }
}
