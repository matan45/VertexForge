#include "MeshDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "print/EditorLogger.hpp"
#include <imgui.h>
#include <fstream>

namespace windows::details
{
    bool MeshDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasMeshComponentQuery hasMeshQuery;
        hasMeshQuery.entity = handle;
        bool hasMesh = dispatcher.query(hasMeshQuery);

        if (!hasMesh)
        {
            return false;
        }

        events::scene::GetMeshDataQuery meshQuery;
        meshQuery.entity = handle;
        auto meshOpt = dispatcher.query(meshQuery);

        if (!meshOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("MeshComponent");

        bool removeMesh = false;
        bool isOpen = drawHeader(removeMesh);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            drawMeshPath(meshOpt->meshPath);
            drawSelectMeshButton(handle, *meshOpt);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            drawAnimatorPath(meshOpt->animatorPath);
            drawAnimatorButtons(handle, *meshOpt);

            ImGui::Spacing();

            drawBoundingBoxCheckbox(handle, *meshOpt);

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

    bool MeshDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##MeshHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Mesh");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveMesh", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    void MeshDrawer::drawMeshPath(const std::string& meshPath)
    {
        if (!meshPath.empty())
        {
            std::string filename = meshPath;
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
    }

    void MeshDrawer::drawSelectMeshButton(services::EntityHandle handle, const services::MeshData& currentData)
    {
        if (!ImGui::Button("Select Mesh"))
        {
            return;
        }

        nfd::FileDialog fileDialog;
        std::string path = fileDialog.openFileDialog(
            {{L"VF Mesh Files (*.vfmesh)", L"*.vfmesh"}});

        if (path.empty())
        {
            return;
        }

        std::ifstream file(path);
        if (file.good())
        {
            file.close();
            auto& dispatcher = events::EventDispatcher::instance();
            events::scene::SetMeshDataCommand cmd;
            cmd.entity = handle;
            cmd.meshData.meshPath = path;
            cmd.meshData.animatorPath = currentData.animatorPath;
            cmd.meshData.showBoundingBox = currentData.showBoundingBox;
            dispatcher.execute(cmd);
        }
        else
        {
            vfLogError("Selected mesh file does not exist or cannot be read: {}", path);
        }
    }

    void MeshDrawer::drawAnimatorPath(const std::string& animatorPath)
    {
        if (!animatorPath.empty())
        {
            std::string filename = animatorPath;
            auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                filename = filename.substr(lastSlash + 1);
            }
            ImGui::Text("Animator: %s", filename.c_str());
        }
        else
        {
            ImGui::TextDisabled("No animator selected");
        }
    }

    void MeshDrawer::drawAnimatorButtons(services::EntityHandle handle, const services::MeshData& currentData)
    {
        if (ImGui::Button("Select Animator"))
        {
            nfd::FileDialog fileDialog;
            std::string path = fileDialog.openFileDialog(
                {{L"VF Animator Files (*.vfAnimator)", L"*.vfAnimator"}});

            if (!path.empty())
            {
                std::ifstream file(path);
                if (file.good())
                {
                    file.close();
                    auto& dispatcher = events::EventDispatcher::instance();
                    events::scene::SetMeshDataCommand cmd;
                    cmd.entity = handle;
                    cmd.meshData.meshPath = currentData.meshPath;
                    cmd.meshData.animatorPath = path;
                    cmd.meshData.showBoundingBox = currentData.showBoundingBox;
                    dispatcher.execute(cmd);
                }
                else
                {
                    vfLogError("Selected animator file does not exist or cannot be read: {}", path);
                }
            }
        }

        if (!currentData.animatorPath.empty())
        {
            ImGui::SameLine();
            if (ImGui::Button("Clear##Animator"))
            {
                auto& dispatcher = events::EventDispatcher::instance();
                events::scene::SetMeshDataCommand cmd;
                cmd.entity = handle;
                cmd.meshData.meshPath = currentData.meshPath;
                cmd.meshData.animatorPath = "";  // Clear animator
                cmd.meshData.showBoundingBox = currentData.showBoundingBox;
                dispatcher.execute(cmd);
            }
        }
    }

    void MeshDrawer::drawBoundingBoxCheckbox(services::EntityHandle handle, const services::MeshData& currentData)
    {
        bool showBoundingBox = currentData.showBoundingBox;
        if (ImGui::Checkbox("Show Bounding Box", &showBoundingBox))
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::scene::SetMeshDataCommand cmd;
            cmd.entity = handle;
            cmd.meshData.meshPath = currentData.meshPath;
            cmd.meshData.animatorPath = currentData.animatorPath;
            cmd.meshData.showBoundingBox = showBoundingBox;
            dispatcher.execute(cmd);
        }
    }
}
