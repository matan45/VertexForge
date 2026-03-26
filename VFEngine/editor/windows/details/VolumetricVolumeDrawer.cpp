#include "VolumetricVolumeDrawer.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/volumetric/VolumetricNavEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include "components/Components.hpp"
#include <imgui.h>

namespace windows::details
{
    static const char* connectivityNames[] = {"6-Connected", "26-Connected"};

    bool VolumetricVolumeDrawer::draw(services::EntityHandle handle)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = services::internal::fromHandle(handle);

        if (!registry.valid(entity) || !registry.all_of<components::VolumetricNavVolumeComponent>(entity))
        {
            return false;
        }

        auto& volume = registry.get<components::VolumetricNavVolumeComponent>(entity);

        bool open = true;
        if (ImGui::CollapsingHeader("Volumetric Nav Volume", &open, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            ImGui::Text("Bounds Min");
            ImGui::PushItemWidth(-1);
            ImGui::DragFloat3("##VolNavBoundsMin", &volume.boundsMin.x, 0.5f, -10000.0f, 10000.0f, "%.1f");
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Text("Bounds Max");
            ImGui::PushItemWidth(-1);
            ImGui::DragFloat3("##VolNavBoundsMax", &volume.boundsMax.x, 0.5f, -10000.0f, 10000.0f, "%.1f");
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Text("Voxel Size");
            ImGui::PushItemWidth(-1);
            ImGui::DragFloat("##VolNavVoxelSize", &volume.voxelSize, 0.01f, 0.1f, 10.0f, "%.2f");
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Text("Connectivity");
            ImGui::PushItemWidth(-1);
            int connIdx = (volume.connectivity == 6) ? 0 : 1;
            if (ImGui::Combo("##VolNavConnectivity", &connIdx, connectivityNames, IM_ARRAYSIZE(connectivityNames)))
            {
                volume.connectivity = (connIdx == 0) ? 6 : 26;
            }
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Text("Agent Clearance");
            ImGui::PushItemWidth(-1);
            ImGui::DragFloat("##VolNavClearance", &volume.agentClearance, 0.01f, 0.0f, 10.0f, "%.2f");
            ImGui::PopItemWidth();

            ImGui::Spacing();
            if (ImGui::Button("Bake", ImVec2(-1, 0)))
            {
                events::volumetric::BakeVolumetricNavCommand cmd;
                cmd.entity = handle;
                events::EventDispatcher::instance().execute(cmd);
            }

            if (ImGui::Button("Clear", ImVec2(-1, 0)))
            {
                events::volumetric::ClearVolumetricNavCommand cmd;
                events::EventDispatcher::instance().execute(cmd);
                volume.isBaked = false;
            }

            ImGui::Spacing();
            if (volume.isBaked)
            {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Baked");
            }
            else
            {
                events::volumetric::GetVolumetricBakeProgressQuery progressQuery;
                auto progress = events::EventDispatcher::instance().query(progressQuery);
                if (progress.status == volumetric::VolumetricBakeStatus::Voxelizing ||
                    progress.status == volumetric::VolumetricBakeStatus::BuildingOctree)
                {
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s (%.0f%%)",
                                       progress.currentStage.c_str(), progress.progress * 100.0f);
                }
                else if (progress.status == volumetric::VolumetricBakeStatus::Failed)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Bake Failed");
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Not Baked");
                }
            }

            ImGui::Unindent();
        }

        if (!open)
        {
            events::scene::RemoveVolumetricNavVolumeComponentCommand cmd;
            cmd.entity = handle;
            events::EventDispatcher::instance().execute(cmd);
            return false;
        }

        return true;
    }
}
