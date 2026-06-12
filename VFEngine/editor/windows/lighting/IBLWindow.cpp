#include "IBLWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include "string/StringUtil.hpp"
#include "asset/AssetRef.hpp"
#include "../../dragdrop/AssetDropTarget.hpp"
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
                if (currentPath != iblPath)
                {
                    selectedIBLFile = iblPath;
                }
            }

            if (ImGui::Button("Select"))
            {
                std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
                    {L"Hdr Files (*.vfHdr)", L"*.vfHdr"}
                };

                selectedIBLFile = fileDialog.openFileDialog(fileTypes);
                std::string filePath = StringUtil::wstringToUtf8(selectedIBLFile.wstring());

                // Update IBL component on root entity via event system
                events::scene::SetIBLDataCommand iblCmd;
                iblCmd.entity = rootHandle;
                iblCmd.iblData.hdrRef = asset::AssetRef::fromPath(filePath);
                dispatcher.execute(iblCmd);
            }

            ImGui::SameLine();
            std::string filePath = StringUtil::wstringToUtf8(selectedIBLFile.wstring());
            ImGui::Text("%s", filePath.empty() ? "(drop a .vfHdr here)" : filePath.c_str());
            if (auto dropped = acceptAssetDropOnLastItem("IBLHdrDrop", {".vfhdr"}))
            {
                selectedIBLFile = *dropped;
                filePath = *dropped;

                events::scene::SetIBLDataCommand iblCmd;
                iblCmd.entity = rootHandle;
                iblCmd.iblData.hdrRef = asset::AssetRef::fromPath(*dropped);
                dispatcher.execute(iblCmd);
            }

            const bool hasFile = !filePath.empty();

            if (!hasFile) ImGui::BeginDisabled();

            if (ImGui::Button("Apply", ImVec2(120, 0)))
            {
                events::render::SetIBLCommand cmd;
                cmd.hdrPath = filePath;
                dispatcher.execute(cmd);
            }
            if (!hasFile) ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::SetCursorPosX(
                ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Close").x - ImGui::GetStyle().FramePadding.x * 2);

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
