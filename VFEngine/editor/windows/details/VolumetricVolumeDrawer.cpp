#include "VolumetricVolumeDrawer.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/volumetric/VolumetricNavEvents.hpp"
#include "navigation/volumetric/VolumetricVoxelizer.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include "components/Components.hpp"
#include <imgui.h>
#include <algorithm>
#include <exception>
#include <optional>
#include <string>
#include <utility>

namespace windows::details
{
    namespace
    {
        const char* connectivityNames[] = {"6-Connected", "26-Connected"};

        const ImVec4 kGoodColor{0.0f, 1.0f, 0.0f, 1.0f};
        const ImVec4 kWarningColor{1.0f, 0.7f, 0.2f, 1.0f};
        const ImVec4 kErrorColor{1.0f, 0.35f, 0.35f, 1.0f};
        const ImVec4 kMutedColor{0.5f, 0.5f, 0.5f, 1.0f};

        // VK-1660: the drawer runs every editor frame on the main thread, so a query nobody answers in this
        // binary (an unregistered handler throws) must read as "no status" instead of escaping the frame.
        std::optional<services::VolumetricVolumeStatus> queryVolumeStatus(services::EntityHandle handle)
        {
            try
            {
                events::volumetric::GetVolumetricVolumeStatusQuery query;
                query.volume = handle;
                return events::EventDispatcher::instance().query(query);
            }
            catch (const std::exception&)
            {
                return std::nullopt;
            }
        }

        bool isPlayMode()
        {
            try
            {
                return events::EventDispatcher::instance().query(events::editor::IsPlayModeQuery{});
            }
            catch (const std::exception&)
            {
                return false;
            }
        }
    }

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

            // The volume is an axis-aligned box around the entity's world position (VK-1660).
            ImGui::Text("Center Offset");
            ImGui::PushItemWidth(-1);
            ImGui::DragFloat3("##VolNavCenterOffset", &volume.centerOffset.x, 0.1f, -10000.0f, 10000.0f, "%.1f");
            ImGui::PopItemWidth();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The box's center relative to the entity's world position");

            ImGui::Spacing();
            ImGui::Text("Half Extents");
            ImGui::PushItemWidth(-1);
            ImGui::DragFloat3("##VolNavHalfExtents", &volume.halfExtents.x, 0.1f, 0.5f, 5000.0f, "%.1f");
            ImGui::PopItemWidth();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Half the box's size on each axis; the entity's rotation and scale are ignored");
            volume.halfExtents.x = std::max(volume.halfExtents.x, 0.5f);
            volume.halfExtents.y = std::max(volume.halfExtents.y, 0.5f);
            volume.halfExtents.z = std::max(volume.halfExtents.z, 0.5f);

            ImGui::Spacing();
            ImGui::Text("Voxel Size");
            ImGui::PushItemWidth(-1);
            ImGui::DragFloat("##VolNavVoxelSize", &volume.voxelSize, 0.01f, 0.1f, 10.0f, "%.2f");
            ImGui::PopItemWidth();
            volume.voxelSize = std::clamp(volume.voxelSize, 0.1f, 10.0f);

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
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Free space kept around geometry: the radius of the largest agent that flies here");
            volume.agentClearance = std::clamp(volume.agentClearance, 0.0f, 10.0f);

            ImGui::Spacing();
            ImGui::Checkbox("Include Terrain##VolNav", &volume.includeTerrain);
            ImGui::Checkbox("Include Static Meshes##VolNav", &volume.includeStaticMeshes);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Meshes and nav-contributing foliage");
            ImGui::Checkbox("Include Colliders##VolNav", &volume.includeColliders);

            // The grid this box and voxel size produce (the bake refuses more than kMaxVoxels).
            ImGui::Spacing();
            const glm::vec3 boxMin = -volume.halfExtents;
            const glm::vec3 boxMax = volume.halfExtents;
            const int64_t voxels = volumetric::voxelCountFor(boxMin, boxMax, volume.voxelSize);
            if (voxels > volumetric::kMaxVoxels)
            {
                ImGui::TextColored(kErrorColor, "%lld voxels: more than the %lld a volume may bake",
                                   static_cast<long long>(voxels), static_cast<long long>(volumetric::kMaxVoxels));
                ImGui::TextColored(kErrorColor, "Raise Voxel Size to %.2f or more",
                                   volumetric::minVoxelSizeFor(boxMin, boxMax));
            }
            else
            {
                const glm::ivec3 dims = volumetric::gridDimensionsFor(boxMin, boxMax, volume.voxelSize);
                ImGui::TextDisabled("Grid %d x %d x %d = %lld voxels", dims.x, dims.y, dims.z,
                                    static_cast<long long>(voxels));
            }

            // The loaded bake.
            ImGui::Spacing();
            const std::optional<services::VolumetricVolumeStatus> status = queryVolumeStatus(handle);
            if (status && status->baked)
            {
                ImGui::TextColored(kGoodColor, "Baked: %d x %d x %d, %.0f%% navigable", status->dims.x, status->dims.y,
                                   status->dims.z, status->navigablePercent);
                if (status->stale)
                    ImGui::TextColored(kWarningColor, "Stale: the volume changed since this bake - bake again");
            }
            else
            {
                ImGui::TextColored(kMutedColor, "Not baked");
            }
            if (status && !status->file.empty())
                ImGui::TextDisabled("File: %s", status->file.c_str());

            ImGui::Spacing();
            drawBakeControls(handle, isPlayMode());

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

    void VolumetricVolumeDrawer::drawBakeControls(services::EntityHandle handle, bool playMode)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Poll the bake this drawer started; a success is saved at once (the scene still has to be saved to
        // keep the new bakedRef).
        if (bakeTicket != 0)
        {
            services::VolumetricBakeOutcome outcome;
            try
            {
                events::volumetric::GetVolumetricBakeOutcomeQuery query;
                query.ticket = bakeTicket;
                outcome = dispatcher.query(query);
            }
            catch (const std::exception& e)
            {
                outcome.state = services::VolumetricBakeState::Failed;
                outcome.message = e.what();
            }

            const services::EntityHandle bakedVolume = outcome.volume.isValid() ? outcome.volume : bakeVolume;
            switch (outcome.state)
            {
            case services::VolumetricBakeState::Running:
                if (bakeVolume == handle)
                {
                    ImGui::ProgressBar(outcome.progress, ImVec2(-1, 0));
                    if (ImGui::Button("Cancel##VolNavBake", ImVec2(-1, 0)))
                    {
                        try
                        {
                            events::volumetric::CancelVolumetricBakeCommand cancel;
                            cancel.volume = bakeVolume;
                            dispatcher.execute(cancel);
                        }
                        catch (const std::exception& e)
                        {
                            showResult(bakeVolume, std::string("Cancel failed: ") + e.what(), true);
                        }
                    }
                }
                break;
            case services::VolumetricBakeState::Succeeded:
            {
                // A bake that finished after Play started is saved once Play stops: a bakedRef set during Play
                // is discarded by the Stop restore, which would orphan the file. The grid stays loaded meanwhile.
                if (playMode)
                {
                    if (bakeVolume == handle)
                        ImGui::TextDisabled("Bake finished - it is saved when Play stops");
                    break;
                }
                bakeTicket = 0;
                services::VolumetricSaveResult saved;
                try
                {
                    events::volumetric::SaveVolumetricVolumeCommand save;
                    save.volume = bakedVolume;
                    saved = dispatcher.execute(save);
                }
                catch (const std::exception& e)
                {
                    saved.error = e.what();
                }
                if (saved.success)
                    showResult(bakedVolume, "Saved to " + saved.path + " - save the scene to keep it", false);
                else
                    showResult(bakedVolume, "Baked, but not saved: " + saved.error, true);
                break;
            }
            case services::VolumetricBakeState::Failed:
                bakeTicket = 0;
                showResult(bakedVolume, "Bake failed: " + outcome.message, true);
                break;
            case services::VolumetricBakeState::Cancelled:
                bakeTicket = 0;
                showResult(bakedVolume, "Bake cancelled", false);
                break;
            case services::VolumetricBakeState::Unknown:
                bakeTicket = 0;
                break;
            }
        }

        // Baking and clearing change the loaded volumes, which Play->Stop keeps: Edit mode only.
        ImGui::BeginDisabled(playMode || bakeTicket != 0);
        if (ImGui::Button("Bake##VolNav", ImVec2(-1, 0)))
        {
            services::VolumetricBakeTicket ticket;
            try
            {
                events::volumetric::RequestVolumetricBakeCommand request;
                request.volume = handle;
                ticket = dispatcher.execute(request);
            }
            catch (const std::exception& e)
            {
                ticket.error = e.what();
            }

            if (ticket.accepted)
            {
                bakeTicket = ticket.ticket;
                bakeVolume = handle;
                resultMessage.clear();
            }
            else
            {
                showResult(handle, "Bake refused: " + ticket.error, true);
            }
        }

        if (ImGui::Button("Clear##VolNav", ImVec2(-1, 0)))
        {
            try
            {
                events::volumetric::ClearVolumetricNavCommand clear;
                clear.volume = handle;
                dispatcher.execute(clear);
                showResult(handle, "Cleared (the .vfVolNav file is kept) - save the scene to keep it", false);
            }
            catch (const std::exception& e)
            {
                showResult(handle, std::string("Clear failed: ") + e.what(), true);
            }
        }
        ImGui::EndDisabled();
        if (playMode)
            ImGui::TextDisabled("Bake and clear in Edit mode");

        if (!resultMessage.empty() && resultVolume == handle)
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(resultIsError ? kErrorColor : kMutedColor, "%s", resultMessage.c_str());
            ImGui::PopTextWrapPos();
        }
    }

    void VolumetricVolumeDrawer::showResult(services::EntityHandle volume, std::string message, bool isError)
    {
        resultVolume = volume;
        resultMessage = std::move(message);
        resultIsError = isError;
    }
}
