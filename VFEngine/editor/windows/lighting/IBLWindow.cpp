#include "IBLWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include "string/StringUtil.hpp"
#include "asset/AssetRef.hpp"
#include "../../dragdrop/AssetDropTarget.hpp"
#include <glm/glm.hpp>
#include <imgui.h>

namespace windows
{
    void IBLWindow::draw()
    {
        if (!visible)
        {
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("IBL", &visible))
        {
            ImGui::Text("IBL Window");

            events::scene::GetRootEntityQuery rootQuery;
            auto rootHandle = dispatcher.query(rootQuery);

            events::scene::GetIBLDataQuery iblQuery;
            iblQuery.entity = rootHandle;
            auto iblData = dispatcher.query(iblQuery);
            if (iblData.has_value() && iblData->hdrRef.isValid())
            {
                // Update local state from scene if different
                std::string currentPath = StringUtil::wstringToUtf8(selectedIBLFile.wstring());
                std::string iblPath = iblData->hdrRef.resolve();
                const bool pathChanged = (currentPath != iblPath);
                if (pathChanged)
                {
                    selectedIBLFile = iblPath;
                }
                // Syncing only on a path change is not enough: IBLDrawer (Entity Details) writes the
                // same three fields, so keeping the stale locals would make the next knob nudge
                // push 1.0/white back over the user's authored intensity and tint — in the renderer
                // AND in the saved scene. Re-read every frame, except while a knob is being dragged.
                if (pathChanged || !knobEditing)
                {
                    iblIntensity = iblData->intensity;
                    iblRotationDeg = iblData->rotationDeg;
                    iblTint[0] = iblData->tint.x;
                    iblTint[1] = iblData->tint.y;
                    iblTint[2] = iblData->tint.z;
                }
            }

            // VK-1574: one-click set — choosing an HDR writes the component AND drives the renderer.
            if (ImGui::Button("Select"))
            {
                std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
                    {L"Hdr Files (*.vfHdr)", L"*.vfHdr"}
                };

                fs::path picked = fileDialog.openFileDialog(fileTypes);
                std::string pickedPath = StringUtil::wstringToUtf8(picked.wstring());
                if (!pickedPath.empty())
                {
                    selectedIBLFile = picked;
                    applyEnvironment(rootHandle, pickedPath, true);
                }
            }

            ImGui::SameLine();
            std::string filePath = StringUtil::wstringToUtf8(selectedIBLFile.wstring());
            ImGui::Text("%s", filePath.empty() ? "(drop a .vfHdr here)" : filePath.c_str());
            if (auto dropped = acceptAssetDropOnLastItem("IBLHdrDrop", {".vfhdr"}))
            {
                selectedIBLFile = *dropped;
                filePath = *dropped;
                applyEnvironment(rootHandle, *dropped, true);
            }

            const bool hasFile = !filePath.empty();

            // VK-1574: live knobs — update the component + renderer with no re-bake.
            if (!hasFile) ImGui::BeginDisabled();

            bool knobChanged = false;
            bool knobActive = false;
            knobChanged |= ImGui::SliderFloat("Intensity", &iblIntensity, 0.0f, 5.0f);
            knobActive |= ImGui::IsItemActive();
            knobChanged |= ImGui::SliderFloat("Rotation", &iblRotationDeg, 0.0f, 360.0f);
            knobActive |= ImGui::IsItemActive();
            knobChanged |= ImGui::ColorEdit3("Tint", iblTint);
            knobActive |= ImGui::IsItemActive();
            knobEditing = knobActive; // consumed by the component re-read at the top of the next frame
            if (knobChanged)
            {
                applyEnvironment(rootHandle, filePath, false);
            }
            if (!hasFile) ImGui::EndDisabled();

            ImGui::Separator();

            if (ImGui::Button("Remove", ImVec2(120, 0)))
            {
                selectedIBLFile = "";

                // Release editor preview texture first
                if (iblPreviewHandle.isValid())
                {
                    events::render::ReleaseEditorTextureCommand releaseCmd;
                    releaseCmd.handle = iblPreviewHandle.imguiDescriptorSet;
                    dispatcher.execute(releaseCmd);
                    iblPreviewHandle = services::EditorTextureHandle{};
                }

                // Then remove IBL from renderer
                events::render::RemoveIBLCommand removeIblCmd;
                dispatcher.execute(removeIblCmd);

                // Remove IBL component from root via event system
                events::scene::GetRootEntityQuery rootQuery2;
                auto rootHandle2 = dispatcher.query(rootQuery2);

                events::scene::RemoveIBLComponentCommand removeCmd;
                removeCmd.entity = rootHandle2;
                dispatcher.execute(removeCmd);
            }

            // Display preview image AFTER all buttons are processed
            // This ensures the descriptor set is valid if we display it
            if (iblPreviewHandle.isValid())
            {
                ImGui::Image(iblPreviewHandle.imguiDescriptorSet, ImVec2(200, 200));
            }
        }
        ImGui::End();
    }

    void IBLWindow::applyEnvironment(services::EntityHandle rootHandle, const std::string& path, bool bake)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // 1) Persist to the scene component (single source of truth): HDR + knobs.
        events::scene::SetIBLDataCommand dataCmd;
        dataCmd.entity = rootHandle;
        dataCmd.iblData.hdrRef = asset::AssetRef::fromPath(path);
        dataCmd.iblData.intensity = iblIntensity;
        dataCmd.iblData.rotationDeg = iblRotationDeg;
        dataCmd.iblData.tint = glm::vec3(iblTint[0], iblTint[1], iblTint[2]);
        dispatcher.execute(dataCmd);

        // 2) Bake the environment only when the HDR itself changed (Select/drop), not on knob edits.
        if (bake && !path.empty())
        {
            events::render::SetIBLCommand setCmd;
            setCmd.hdrPath = path;
            dispatcher.execute(setCmd);
        }

        // 3) Push live knobs to the renderer (applied in the ambient block; no re-bake).
        events::render::SetIBLParamsCommand paramsCmd;
        paramsCmd.intensity = iblIntensity;
        paramsCmd.rotationDeg = iblRotationDeg;
        paramsCmd.tint = glm::vec3(iblTint[0], iblTint[1], iblTint[2]);
        dispatcher.execute(paramsCmd);
    }

    void IBLWindow::onSceneCleared()
    {
        // Release IBL preview texture if it exists
        if (iblPreviewHandle.isValid())
        {
            events::render::ReleaseEditorTextureCommand releaseCmd;
            releaseCmd.handle = iblPreviewHandle.imguiDescriptorSet;
            events::EventDispatcher::instance().execute(releaseCmd);
            iblPreviewHandle = services::EditorTextureHandle{};
        }

        // Clear IBL file selection
        selectedIBLFile = "";
    }
}
