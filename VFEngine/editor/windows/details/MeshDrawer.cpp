#include "print/Log.hpp"
#include "MeshDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
#include "../../dragdrop/AssetDropTarget.hpp"
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

            drawMeshPath(meshOpt->meshRef.resolve());
            if (auto dropped = acceptAssetDropOnLastItem("MeshSlotDrop", {".vfmesh"}))
            {
                events::scene::SetMeshDataCommand cmd;
                cmd.entity = handle;
                cmd.meshData = *meshOpt;
                cmd.meshData.meshRef = asset::AssetRef::fromPath(*dropped);
                dispatcher.execute(cmd);
            }
            drawSelectMeshButton(handle, *meshOpt);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            drawAnimatorPath(meshOpt->animatorRef.resolve());
            if (auto dropped = acceptAssetDropOnLastItem("AnimatorSlotDrop", {".vfanimator"}))
            {
                events::scene::SetMeshDataCommand cmd;
                cmd.entity = handle;
                cmd.meshData = *meshOpt;
                cmd.meshData.animatorRef = asset::AssetRef::fromPath(*dropped);
                dispatcher.execute(cmd);
            }
            drawAnimatorButtons(handle, *meshOpt);

            if (meshOpt->animatorRef.isValid())
            {
                drawRootMotionCheckbox(handle, *meshOpt);
            }

            ImGui::Spacing();

            // Retarget binding (VK-910): plays this animator's clips through a
            // source->target skeleton retarget at runtime. Drag a .vfretarget here.
            {
                const std::string retargetPath = meshOpt->retargetRef.resolve();
                if (!retargetPath.empty())
                {
                    std::string filename = retargetPath;
                    auto lastSlash = filename.find_last_of("/\\");
                    if (lastSlash != std::string::npos)
                        filename = filename.substr(lastSlash + 1);
                    ImGui::Text("Retarget: %s", filename.c_str());
                }
                else
                {
                    ImGui::TextDisabled("No retarget (native skeleton)");
                }

                if (auto dropped = acceptAssetDropOnLastItem("RetargetSlotDrop", {".vfretarget"}))
                {
                    events::scene::SetMeshDataCommand cmd;
                    cmd.entity = handle;
                    cmd.meshData = *meshOpt;
                    cmd.meshData.retargetRef = asset::AssetRef::fromPath(*dropped);
                    dispatcher.execute(cmd);
                }

                if (ImGui::Button("Select Retarget"))
                {
                    nfd::FileDialog fileDialog;
                    std::string path = fileDialog.openFileDialog(
                        {{L"VF Retarget Files (*.vfretarget)", L"*.vfretarget"}});
                    if (!path.empty())
                    {
                        events::scene::SetMeshDataCommand cmd;
                        cmd.entity = handle;
                        cmd.meshData = *meshOpt;
                        cmd.meshData.retargetRef = asset::AssetRef::fromPath(path);
                        dispatcher.execute(cmd);
                    }
                }
                if (meshOpt->retargetRef.isValid())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Clear##Retarget"))
                    {
                        events::scene::SetMeshDataCommand cmd;
                        cmd.entity = handle;
                        cmd.meshData = *meshOpt;
                        cmd.meshData.retargetRef = asset::AssetRef::invalid();
                        dispatcher.execute(cmd);
                    }
                }
            }

            ImGui::Spacing();

            drawBoundingBoxCheckbox(handle, *meshOpt);
            drawMaxDrawDistance(handle, *meshOpt);
            drawSubmeshIndex(handle, *meshOpt);

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
            cmd.meshData = currentData;
            cmd.meshData.meshRef = asset::AssetRef::fromPath(path);
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
                    cmd.meshData = currentData;
                    cmd.meshData.animatorRef = asset::AssetRef::fromPath(path);
                    dispatcher.execute(cmd);
                }
                else
                {
                    vfLogError("Selected animator file does not exist or cannot be read: {}", path);
                }
            }
        }

        if (currentData.animatorRef.isValid())
        {
            ImGui::SameLine();
            if (ImGui::Button("Clear##Animator"))
            {
                auto& dispatcher = events::EventDispatcher::instance();
                events::scene::SetMeshDataCommand cmd;
                cmd.entity = handle;
                cmd.meshData = currentData;
                cmd.meshData.animatorRef = asset::AssetRef::invalid();  // Clear animator
                cmd.meshData.applyRootMotion = false;  // Reset when clearing animator
                dispatcher.execute(cmd);
            }
        }
    }

    void MeshDrawer::drawRootMotionCheckbox(services::EntityHandle handle, const services::MeshData& currentData)
    {
        bool applyRootMotion = currentData.applyRootMotion;
        if (ImGui::Checkbox("Apply Root Motion", &applyRootMotion))
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::scene::SetMeshDataCommand cmd;
            cmd.entity = handle;
            cmd.meshData = currentData;
            cmd.meshData.applyRootMotion = applyRootMotion;
            dispatcher.execute(cmd);
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
            cmd.meshData = currentData;
            cmd.meshData.showBoundingBox = showBoundingBox;
            dispatcher.execute(cmd);
        }
    }

    void MeshDrawer::drawMaxDrawDistance(services::EntityHandle handle, const services::MeshData& currentData)
    {
        float maxDrawDist = currentData.maxDrawDistance;
        if (ImGui::DragFloat("Max Draw Distance", &maxDrawDist, 10.0f, 0.0f, 50000.0f, "%.0f"))
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::scene::SetMeshDataCommand cmd;
            cmd.entity = handle;
            cmd.meshData = currentData;
            cmd.meshData.maxDrawDistance = maxDrawDist;
            dispatcher.execute(cmd);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Override draw distance for this entity.\n0 = use category default from Render Config.");
        }
    }

    void MeshDrawer::drawSubmeshIndex(services::EntityHandle handle, const services::MeshData& currentData)
    {
        int submeshIndex = currentData.submeshIndex;
        if (ImGui::InputInt("Submesh Index", &submeshIndex))
        {
            if (submeshIndex < -1)
                submeshIndex = -1;

            auto& dispatcher = events::EventDispatcher::instance();
            events::scene::SetMeshDataCommand cmd;
            cmd.entity = handle;
            cmd.meshData = currentData;
            cmd.meshData.submeshIndex = submeshIndex;
            dispatcher.execute(cmd);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("-1 = render all submeshes\n>= 0 = render only the specified submesh");
        }
    }
}
