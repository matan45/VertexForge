#include "MaterialDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "events/MaterialEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "resource/MeshResource.hpp"
#include <imgui.h>

namespace windows::details {

    void MaterialDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::GetMeshDataQuery meshDataQuery;
        meshDataQuery.entity = handle;
        auto meshDataOpt = dispatcher.query(meshDataQuery);

        if (!meshDataOpt.has_value() || meshDataOpt->meshPath.empty())
            return;

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
            return;

        events::material::GetMaterialDataQuery matQuery;
        matQuery.entity = handle;
        auto matOpt = dispatcher.query(matQuery);

        if (!matOpt.has_value())
            return;

        ImGui::PushID("MaterialComponent");

        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isMatOpen = ImGui::CollapsingHeader("##MaterialHeader",
                                                 ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Materials");
        EntityDetailsPanel::popComponentHeaderStyle();

        if (isMatOpen)
        {
            ImGui::Indent(10.0f);

            // Default Material
            ImGui::Text("Default Material:");
            std::string defaultMatDisplay = matOpt->defaultMaterial.empty() ? "(None)" : matOpt->defaultMaterial;
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

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Collapsible submesh materials section
            if (ImGui::CollapsingHeader("Submesh Materials", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent(10.0f);

                const std::string& meshPath = meshDataOpt->meshPath;
                auto cacheIt = submeshNameCache.find(meshPath);
                if (cacheIt == submeshNameCache.end())
                {
                    try
                    {
                        resource::MeshesData meshData = resource::MeshResource::loadMesh(meshPath);
                        std::vector<std::string> names;
                        for (size_t i = 0; i < meshData.meshes.size(); ++i)
                        {
                            const auto& submesh = meshData.meshes[i];
                            names.push_back(submesh.name.empty() ? "SubMesh_" + std::to_string(i) : submesh.name);
                        }
                        cacheIt = submeshNameCache.emplace(meshPath, std::move(names)).first;
                    }
                    catch (const std::exception& e)
                    {
                        ImGui::TextDisabled("Failed to load mesh: %s", e.what());
                        cacheIt = submeshNameCache.emplace(meshPath, std::vector<std::string>{}).first;
                    }
                }

                const auto& submeshNames = cacheIt->second;
                if (submeshNames.empty())
                {
                    ImGui::TextDisabled("No submeshes found");
                }
                else
                {
                    for (size_t i = 0; i < submeshNames.size(); ++i)
                    {
                        const std::string& submeshName = submeshNames[i];

                        ImGui::PushID(static_cast<int>(i));

                        auto it = matOpt->subMeshMaterials.find(submeshName);
                        std::string currentMat = (it != matOpt->subMeshMaterials.end()) ? it->second : "";

                        ImGui::BulletText("%s", submeshName.c_str());
                        ImGui::Indent(20.0f);

                        std::string matDisplay = currentMat.empty() ? "(Default)" : currentMat;
                        if (matDisplay.length() > 30)
                        {
                            matDisplay = "..." + matDisplay.substr(matDisplay.length() - 27);
                        }
                        ImGui::TextDisabled("%s", matDisplay.c_str());

                        ImGui::SameLine();
                        std::string browseId = "Browse##submesh" + std::to_string(i);
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
                        std::string clearId = "Clear##submesh" + std::to_string(i);
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
                }

                ImGui::Unindent(10.0f);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();
    }

    void MaterialDrawer::clearCache()
    {
        submeshNameCache.clear();
    }

}
