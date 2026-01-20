#include "VFXDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "events/VFXRuntimeEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "print/EditorLogger.hpp"
#include <imgui.h>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>

namespace windows::details
{
    bool VFXDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasVFXComponentQuery hasVFXQuery;
        hasVFXQuery.entity = handle;
        bool hasVFX = dispatcher.query(hasVFXQuery);

        if (!hasVFX)
        {
            return false;
        }

        events::scene::GetVFXDataQuery vfxQuery;
        vfxQuery.entity = handle;
        auto vfxOpt = dispatcher.query(vfxQuery);

        if (!vfxOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("VFXComponent");

        bool removeVFX = false;
        bool isOpen = drawHeader(removeVFX);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::VFXData vfxData = *vfxOpt;
            bool changed = false;

            ImGui::TextDisabled("Visual effects particle system");
            ImGui::Spacing();

            changed |= drawVFXFilePath(vfxData);
            ImGui::Spacing();
            changed |= drawSettings(vfxData);

            if (changed)
            {
                events::scene::SetVFXDataCommand cmd;
                cmd.entity = handle;
                cmd.vfxData = vfxData;
                dispatcher.execute(cmd);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            drawPlaybackControls(handle, vfxData);

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeVFX)
        {
            events::scene::RemoveVFXComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);

            // Clean up any editor preview instance
            uint64_t previewKey = handle.id;
            auto it = vfxPreviewInstances.find(previewKey);
            if (it != vfxPreviewInstances.end())
            {
                services::events::vfxruntime::DestroyVFXInstanceCommand destroyCmd;
                destroyCmd.instanceId = it->second;
                dispatcher.execute(destroyCmd);
                vfxPreviewInstances.erase(it);
            }
        }

        return true;
    }

    bool VFXDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##VFXHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("VFX");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveVFX", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool VFXDrawer::drawVFXFilePath(services::VFXData& vfxData)
    {
        bool changed = false;

        if (!vfxData.vfxPath.empty())
        {
            std::string filename = vfxData.vfxPath;
            auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                filename = filename.substr(lastSlash + 1);
            }
            ImGui::Text("File: %s", filename.c_str());
        }
        else
        {
            ImGui::TextDisabled("No VFX file selected");
        }

        if (ImGui::Button("Select VFX File##VFX"))
        {
            nfd::FileDialog fileDialog;
            std::string path = fileDialog.openFileDialog(
                {{L"VF VFX Files (*.vfVFX)", L"*.vfVFX"}});
            if (!path.empty())
            {
                std::ifstream file(path);
                if (file.good())
                {
                    file.close();
                    vfxData.vfxPath = path;
                    changed = true;
                }
                else
                {
                    vfLogError("Selected VFX file does not exist or cannot be read: {}", path);
                }
            }
        }

        ImGui::SameLine();
        if (vfxData.vfxPath.empty()) ImGui::BeginDisabled();
        if (ImGui::Button("Clear##VFX"))
        {
            vfxData.vfxPath = "";
            changed = true;
        }
        if (vfxData.vfxPath.empty()) ImGui::EndDisabled();

        return changed;
    }

    bool VFXDrawer::drawSettings(services::VFXData& vfxData)
    {
        bool changed = false;

        if (ImGui::Checkbox("Auto Play##VFX", &vfxData.autoPlay))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Automatically play when entering play mode");
        }

        if (ImGui::Checkbox("Loop##VFX", &vfxData.loop))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Loop the VFX effect continuously");
        }

        return changed;
    }

    void VFXDrawer::drawPlaybackControls(services::EntityHandle handle, const services::VFXData& vfxData)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        uint64_t previewKey = handle.id;
        auto previewIt = vfxPreviewInstances.find(previewKey);
        bool hasPreviewInstance = previewIt != vfxPreviewInstances.end();

        // Check if instance is still active
        bool isInstanceActive = false;
        if (hasPreviewInstance)
        {
            services::events::vfxruntime::IsVFXInstanceActiveQuery activeQuery;
            activeQuery.instanceId = previewIt->second;
            isInstanceActive = dispatcher.query(activeQuery);
        }

        // Clean up invalid instances
        if (hasPreviewInstance && !isInstanceActive)
        {
            vfxPreviewInstances.erase(previewIt);
            hasPreviewInstance = false;
            previewIt = vfxPreviewInstances.end();
        }

        // Check if currently playing
        bool isCurrentlyPlaying = false;
        if (hasPreviewInstance)
        {
            services::events::vfxruntime::IsVFXInstancePlayingQuery playingQuery;
            playingQuery.instanceId = previewIt->second;
            isCurrentlyPlaying = dispatcher.query(playingQuery);
        }

        ImGui::TextDisabled("Editor Preview:");

        // Play button
        bool canPlay = !vfxData.vfxPath.empty() && !isCurrentlyPlaying;
        if (!canPlay) ImGui::BeginDisabled();
        if (ImGui::Button("Play##VFX", ImVec2(60, 0)))
        {
            if (hasPreviewInstance)
            {
                // Resume existing instance
                services::events::vfxruntime::PlayVFXInstanceCommand playCmd;
                playCmd.instanceId = previewIt->second;
                dispatcher.execute(playCmd);
            }
            else
            {
                // Get world transform for the VFX instance
                events::scene::GetTransformQuery transformQuery;
                transformQuery.entity = handle;
                auto transformOpt = dispatcher.query(transformQuery);

                glm::mat4 worldTransform = glm::mat4(1.0f);
                if (transformOpt.has_value())
                {
                    worldTransform = glm::translate(glm::mat4(1.0f), transformOpt->position);
                    // Apply rotation
                    worldTransform = glm::rotate(worldTransform, glm::radians(transformOpt->rotation.x), glm::vec3(1, 0, 0));
                    worldTransform = glm::rotate(worldTransform, glm::radians(transformOpt->rotation.y), glm::vec3(0, 1, 0));
                    worldTransform = glm::rotate(worldTransform, glm::radians(transformOpt->rotation.z), glm::vec3(0, 0, 1));
                    worldTransform = glm::scale(worldTransform, transformOpt->scale);
                }

                // Create new instance
                services::events::vfxruntime::CreateVFXInstanceCommand createCmd;
                createCmd.params.vfxAssetPath = vfxData.vfxPath;
                createCmd.params.worldTransform = worldTransform;
                createCmd.params.loop = vfxData.loop;

                services::VFXInstanceId newInstance = dispatcher.execute(createCmd);
                if (newInstance != 0)
                {
                    vfxPreviewInstances[previewKey] = newInstance;

                    services::events::vfxruntime::PlayVFXInstanceCommand playCmd;
                    playCmd.instanceId = newInstance;
                    dispatcher.execute(playCmd);
                }
            }
        }
        if (!canPlay) ImGui::EndDisabled();

        // Stop button
        ImGui::SameLine();
        if (!hasPreviewInstance) ImGui::BeginDisabled();
        if (ImGui::Button("Stop##VFX", ImVec2(60, 0)))
        {
            if (hasPreviewInstance)
            {
                services::events::vfxruntime::StopVFXInstanceCommand stopCmd;
                stopCmd.instanceId = previewIt->second;
                dispatcher.execute(stopCmd);
            }
        }
        if (!hasPreviewInstance) ImGui::EndDisabled();

        // Reset button
        ImGui::SameLine();
        if (!hasPreviewInstance) ImGui::BeginDisabled();
        if (ImGui::Button("Reset##VFX", ImVec2(60, 0)))
        {
            if (hasPreviewInstance)
            {
                services::events::vfxruntime::ResetVFXInstanceCommand resetCmd;
                resetCmd.instanceId = previewIt->second;
                dispatcher.execute(resetCmd);
            }
        }
        if (!hasPreviewInstance) ImGui::EndDisabled();
    }

    void VFXDrawer::clearInstances()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        for (const auto& [key, instanceId] : vfxPreviewInstances)
        {
            services::events::vfxruntime::DestroyVFXInstanceCommand destroyCmd;
            destroyCmd.instanceId = instanceId;
            dispatcher.execute(destroyCmd);
        }

        vfxPreviewInstances.clear();
    }
}
