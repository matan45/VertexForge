#include "MaterialDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "events/MaterialEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "resource/MeshStreamHandle.hpp"
#include <imgui.h>

namespace windows::details
{
    void MaterialDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::GetMeshDataQuery meshDataQuery;
        meshDataQuery.entity = handle;
        auto meshDataOpt = dispatcher.query(meshDataQuery);

        if (!meshDataOpt.has_value() || meshDataOpt->meshPath.empty())
        {
            return;
        }

        events::material::HasMaterialComponentQuery hasMaterialQuery;
        hasMaterialQuery.entity = handle;
        bool hasMaterial = dispatcher.query(hasMaterialQuery);

        if (!hasMaterial)
        {
            events::material::AddMaterialComponentCommand addMatCmd;
            addMatCmd.entity = handle;
            dispatcher.execute(addMatCmd);
            hasMaterial = true;
        }

        if (!hasMaterial)
        {
            return;
        }

        events::material::GetMaterialDataQuery matQuery;
        matQuery.entity = handle;
        auto matOpt = dispatcher.query(matQuery);

        if (!matOpt.has_value())
        {
            return;
        }

        ImGui::PushID("MaterialComponent");

        bool isOpen = drawHeader();

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            drawDefaultMaterial(handle, *matOpt);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            drawSubmeshMaterials(handle, meshDataOpt->meshPath, *matOpt);

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();
    }

    bool MaterialDrawer::drawHeader()
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##MaterialHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Materials");
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    void MaterialDrawer::drawDefaultMaterial(services::EntityHandle handle, const services::MaterialData& matData)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::Text("Default Material:");
        std::string defaultMatDisplay = matData.defaultMaterial.empty() ? "(None)" : matData.defaultMaterial;
        if (defaultMatDisplay.length() > 35)
        {
            defaultMatDisplay = "..." + defaultMatDisplay.substr(defaultMatDisplay.length() - 32);
        }
        ImGui::TextDisabled("%s", defaultMatDisplay.c_str());

        ImGui::SameLine();
        if (ImGui::Button("Browse##DefaultMat"))
        {
            nfd::FileDialog fileDialog;
            std::string path = fileDialog.openFileDialog({
                {L"VF Material (*.vfMat, *.vfMatInstance)", L"*.vfMat;*.vfMatInstance"}
            });
            if (!path.empty())
            {
                events::material::SetDefaultMaterialCommand cmd;
                cmd.entity = handle;
                cmd.materialPath = path;
                dispatcher.execute(cmd);
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear##DefaultMat"))
        {
            events::material::SetDefaultMaterialCommand cmd;
            cmd.entity = handle;
            cmd.materialPath = "";
            dispatcher.execute(cmd);
        }
    }

    void MaterialDrawer::drawSubmeshMaterials(services::EntityHandle handle,
                                              const std::string& meshPath,
                                              const services::MaterialData& matData)
    {
        if (!ImGui::CollapsingHeader("Submesh Materials", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        ImGui::Indent(10.0f);

        const auto& submeshNames = getSubmeshNames(meshPath);

        if (submeshNames.empty())
        {
            ImGui::TextDisabled("No submeshes found");
        }
        else
        {
            for (size_t i = 0; i < submeshNames.size(); ++i)
            {
                const std::string& submeshName = submeshNames[i];
                auto it = matData.subMeshMaterials.find(submeshName);
                std::string currentMat = (it != matData.subMeshMaterials.end()) ? it->second : "";

                drawSubmeshEntry(handle, submeshName, currentMat, static_cast<int>(i));
            }
        }

        ImGui::Unindent(10.0f);
    }

    void MaterialDrawer::drawSubmeshEntry(services::EntityHandle handle,
                                          const std::string& submeshName,
                                          const std::string& currentMat,
                                          int index)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::PushID(index);

        ImGui::BulletText("%s", submeshName.c_str());
        ImGui::Indent(20.0f);

        std::string matDisplay = currentMat.empty() ? "(Default)" : currentMat;
        if (matDisplay.length() > 30)
        {
            matDisplay = "..." + matDisplay.substr(matDisplay.length() - 27);
        }
        ImGui::TextDisabled("%s", matDisplay.c_str());

        ImGui::SameLine();
        std::string browseId = "Browse##submesh" + std::to_string(index);
        if (ImGui::Button(browseId.c_str()))
        {
            nfd::FileDialog fileDialog;
            std::string path = fileDialog.openFileDialog({
                {L"VF Material (*.vfMat, *.vfMatInstance)", L"*.vfMat;*.vfMatInstance"}
            });
            if (!path.empty())
            {
                events::material::SetSubMeshMaterialCommand cmd;
                cmd.entity = handle;
                cmd.submeshName = submeshName;
                cmd.materialPath = path;
                dispatcher.execute(cmd);
            }
        }

        ImGui::SameLine();
        std::string clearId = "Clear##submesh" + std::to_string(index);
        if (ImGui::Button(clearId.c_str()))
        {
            events::material::SetSubMeshMaterialCommand cmd;
            cmd.entity = handle;
            cmd.submeshName = submeshName;
            cmd.materialPath = "";
            dispatcher.execute(cmd);
        }

        ImGui::Unindent(20.0f);
        ImGui::PopID();
    }

    const std::vector<std::string>& MaterialDrawer::getSubmeshNames(const std::string& meshPath)
    {
        auto cacheIt = submeshNameCache.find(meshPath);
        if (cacheIt != submeshNameCache.end())
        {
            return cacheIt->second;
        }

        std::vector<std::string> names;
        auto stream = resource::MeshStreamResource::openStream(meshPath);
        if (stream)
        {
            const auto& header = stream->getHeader();
            for (uint32_t i = 0; i < header.numSubmeshes; ++i)
            {
                const auto& name = header.submeshes[i].name;
                names.push_back(name.empty() ? "SubMesh_" + std::to_string(i) : name);
            }
        }
        cacheIt = submeshNameCache.emplace(meshPath, std::move(names)).first;

        return cacheIt->second;
    }

    void MaterialDrawer::clearCache()
    {
        submeshNameCache.clear();
    }
}
