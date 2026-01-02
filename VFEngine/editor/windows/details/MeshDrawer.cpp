#include "MeshDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "print/EditorLogger.hpp"
#include <imgui.h>
#include <fstream>

namespace windows::details {

    bool MeshDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasMeshComponentQuery hasMeshQuery;
        hasMeshQuery.entity = handle;
        bool hasMesh = dispatcher.query(hasMeshQuery);

        if (!hasMesh)
            return false;

        events::scene::GetMeshDataQuery meshQuery;
        meshQuery.entity = handle;
        auto meshOpt = dispatcher.query(meshQuery);

        if (!meshOpt.has_value())
            return true;

        ImGui::PushID("MeshComponent");

        bool removeMesh = false;

        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##MeshHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Mesh");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveMesh", ImVec2(18, 18)))
        {
            removeMesh = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            if (!meshOpt->meshPath.empty())
            {
                std::string filename = meshOpt->meshPath;
                auto lastSlash = filename.find_last_of("/\\");
                if (lastSlash != std::string::npos)
                {
                    filename = filename.substr(lastSlash + 1);
                }
                ImGui::Text("Mesh: %s", filename.c_str());
            }
            else
            {
                ImGui::TextDisabled("No mesh selected");
            }

            if (ImGui::Button("Select Mesh"))
            {
                nfd::FileDialog fileDialog;
                std::string path = fileDialog.openFileDialog(
                    {{L"VF Mesh Files (*.vfmesh)", L"*.vfmesh"}});
                if (!path.empty())
                {
                    std::ifstream file(path);
                    if (file.good())
                    {
                        file.close();
                        events::scene::SetMeshDataCommand cmd;
                        cmd.entity = handle;
                        cmd.meshData.meshPath = path;
                        cmd.meshData.showBoundingBox = meshOpt->showBoundingBox;
                        dispatcher.execute(cmd);
                    }
                    else
                    {
                        vfLogError("Selected mesh file does not exist or cannot be read: {}", path);
                    }
                }
            }

            bool showBoundingBox = meshOpt->showBoundingBox;
            if (ImGui::Checkbox("Show Bounding Box", &showBoundingBox))
            {
                events::scene::SetMeshDataCommand cmd;
                cmd.entity = handle;
                cmd.meshData.meshPath = meshOpt->meshPath;
                cmd.meshData.showBoundingBox = showBoundingBox;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeMesh)
        {
            events::scene::RemoveMeshComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

}
