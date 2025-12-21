#include "MeshPreviewWindow.hpp"
#include "../camera/OrbitCamera.hpp"
#include "imgui.h"
#include "print/EditorLogger.hpp"
#include "events/EventDispatcher.hpp"
#include "events/PreviewEvents.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>

namespace windows
{
    MeshPreviewWindow::MeshPreviewWindow(const std::string& meshFilePath)
        : meshPath(meshFilePath)
          , camera(std::make_unique<editor::OrbitCamera>())
    {
        std::filesystem::path path(meshFilePath);
        windowTitle = "Mesh Preview: " + path.filename().string();
    }

    MeshPreviewWindow::~MeshPreviewWindow()
    {
        services::events::preview::UnloadPreviewMeshCommand unloadCmd;
        unloadCmd.instanceId = services::PreviewInstanceId(this);
        events::EventDispatcher::instance().execute(unloadCmd);

        services::events::preview::CleanUpMeshPreviewCommand cleanupCmd;
        cleanupCmd.instanceId = services::PreviewInstanceId(this);
        events::EventDispatcher::instance().execute(cleanupCmd);
    }

    void MeshPreviewWindow::draw()
    {
        if (!isOpen)
        {
            return;
        }

        // Initialize on first draw
        if (needsInit)
        {
            initRenderer();
            needsInit = false;
        }

        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse))
        {
            if (isOpen)
            {
                // Split layout: left panel for submesh list, right for 3D viewport
                float panelWidth = 200.0f;
                ImVec2 contentSize = ImGui::GetContentRegionAvail();

                // Submesh panel on the left
                ImGui::BeginChild("SubMeshPanel", ImVec2(panelWidth, contentSize.y), true);
                drawSubMeshPanel();
                ImGui::EndChild();

                ImGui::SameLine();

                // 3D viewport on the right
                float viewportWidth = contentSize.x - panelWidth - ImGui::GetStyle().ItemSpacing.x;
                ImGui::BeginChild("ViewportPanel", ImVec2(viewportWidth, contentSize.y), true,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                ImVec2 viewportSize = ImGui::GetContentRegionAvail();
                drawViewport(viewportSize.x, viewportSize.y);

                ImGui::EndChild();
            }
        }
        ImGui::End();
    }

    void MeshPreviewWindow::initRenderer()
    {
        services::events::preview::InitMeshPreviewCommand initCmd;
        initCmd.instanceId = services::PreviewInstanceId(this);
        events::EventDispatcher::instance().execute(initCmd);

        services::events::preview::LoadPreviewMeshCommand loadCmd;
        loadCmd.instanceId = services::PreviewInstanceId(this);
        loadCmd.meshPath = meshPath;
        auto result = events::EventDispatcher::instance().execute(loadCmd);

        if (result.success)
        {
            meshBounds = result.bounds;
            camera->fitToBounds(meshBounds);

            services::events::preview::GetPreviewMeshSubMeshInfoQuery subMeshQuery;
            subMeshQuery.instanceId = services::PreviewInstanceId(this);
            subMeshes = events::EventDispatcher::instance().query(subMeshQuery);

            services::events::preview::GetPreviewMeshLODInfoQuery lodQuery;
            lodQuery.instanceId = services::PreviewInstanceId(this);
            lodLevels = events::EventDispatcher::instance().query(lodQuery);
        }
    }

    void MeshPreviewWindow::drawViewport(float width, float height)
    {
        if (width <= 0 || height <= 0)
        {
            return;
        }
        camera->setAspectRatio(width / height);

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, meshPosition);
        model = glm::rotate(model, glm::radians(meshRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(meshRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(meshRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, glm::vec3(meshScale));

        services::MeshPreviewParams meshParams;
        meshParams.modelMatrix = model;
        meshParams.highlightedSubMesh = selectedSubMesh;
        meshParams.forceLODLevel = selectedLOD;

        services::events::preview::SetMeshPreviewParamsCommand meshCmd;
        meshCmd.instanceId = services::PreviewInstanceId(this);
        meshCmd.params = meshParams;
        events::EventDispatcher::instance().execute(meshCmd);

        services::events::preview::UpdateMeshCameraCommand cameraCmd;
        cameraCmd.instanceId = services::PreviewInstanceId(this);
        cameraCmd.view = camera->getViewMatrix();
        cameraCmd.projection = camera->getProjectionMatrix();
        cameraCmd.cameraPos = camera->getPosition();
        events::EventDispatcher::instance().execute(cameraCmd);

        // Render via PreviewService
        services::events::preview::RenderMeshPreviewQuery renderQuery;
        renderQuery.instanceId = services::PreviewInstanceId(this);
        auto textureHandle = events::EventDispatcher::instance().query(renderQuery);

        if (textureHandle.imguiDescriptorSet)
        {
            ImGui::Image(textureHandle.imguiDescriptorSet, ImVec2(width, height));
        }
    }

    void MeshPreviewWindow::drawSubMeshPanel()
    {
        ImGui::Text("Submeshes");
        ImGui::Separator();

        if (subMeshes.empty())
        {
            ImGui::TextDisabled("No submeshes");
            return;
        }

        // "All" option
        bool allSelected = (selectedSubMesh == -1);
        if (ImGui::Selectable("All Submeshes", allSelected))
        {
            selectedSubMesh = -1;
        }

        ImGui::Separator();

        // Individual submeshes
        for (size_t i = 0; i < subMeshes.size(); ++i)
        {
            const auto& info = subMeshes[i];
            bool isSelected = (selectedSubMesh == static_cast<int>(i));

            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(info.name.c_str(), isSelected))
            {
                selectedSubMesh = static_cast<int>(i);
            }

            // Show tooltip with details
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Vertices: %u", info.vertexCount);
                ImGui::Text("Indices: %u", info.indexCount);
                ImGui::Text("Triangles: %u", info.indexCount / 3);
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }

        ImGui::Separator();

        // LOD Level Selection
        if (!lodLevels.empty())
        {
            ImGui::Spacing();
            ImGui::Text("LOD Level");

            const char* lodLabels[] = {"Auto", "LOD 0 (100%)", "LOD 1 (50%)", "LOD 2 (25%)", "LOD 3 (12.5%)"};
            int currentLOD = selectedLOD + 1; // -1 becomes 0 (Auto), 0 becomes 1 (LOD 0), etc.

            float itemWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetNextItemWidth(itemWidth);
            if (ImGui::Combo("##LODLevel", &currentLOD, lodLabels, 5))
            {
                selectedLOD = currentLOD - 1; // 0 becomes -1 (Auto), 1 becomes 0 (LOD 0), etc.
            }


            int displayLOD = (selectedLOD >= 0) ? selectedLOD : 0;
            if (static_cast<size_t>(displayLOD) < lodLevels.size())
            {
                const auto& lodInfo = lodLevels[displayLOD];
                if (selectedLOD < 0)
                {
                    ImGui::TextDisabled("Auto (showing LOD0):");
                }
                else
                {
                    ImGui::TextDisabled("LOD %d (%.1f%%):", lodInfo.lodLevel, lodInfo.reductionPercent);
                }
                ImGui::Text("  Vertices: %u", lodInfo.vertexCount);
                ImGui::Text("  Triangles: %u", lodInfo.indexCount / 3);
            }

            ImGui::Separator();
        }

        // Summary
        uint32_t totalVerts = 0;
        uint32_t totalIndices = 0;
        for (const auto& info : subMeshes)
        {
            totalVerts += info.vertexCount;
            totalIndices += info.indexCount;
        }

        ImGui::TextDisabled("Total (LOD0):");
        ImGui::Text("  Submeshes: %zu", subMeshes.size());
        ImGui::Text("  Vertices: %u", totalVerts);
        ImGui::Text("  Triangles: %u", totalIndices / 3);

        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            float itemWidth = ImGui::GetContentRegionAvail().x - 50.0f;

            ImGui::Text("Pos X");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            ImGui::DragFloat("##PosX", &meshPosition.x, 0.1f, -1000.0f, 1000.0f, "%.2f");

            ImGui::Text("Pos Y");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            ImGui::DragFloat("##PosY", &meshPosition.y, 0.1f, -1000.0f, 1000.0f, "%.2f");

            ImGui::Text("Pos Z");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            ImGui::DragFloat("##PosZ", &meshPosition.z, 0.1f, -1000.0f, 1000.0f, "%.2f");

            ImGui::Spacing();

            ImGui::Text("Rot X");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            ImGui::SliderFloat("##RotX", &meshRotation.x, -180.0f, 180.0f, "%.0f");

            ImGui::Text("Rot Y");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            ImGui::SliderFloat("##RotY", &meshRotation.y, -180.0f, 180.0f, "%.0f");

            ImGui::Text("Rot Z");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            ImGui::SliderFloat("##RotZ", &meshRotation.z, -180.0f, 180.0f, "%.0f");

            ImGui::Spacing();

            ImGui::Text("Scale");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            if (ImGui::SliderFloat("##Scale", &meshScale, 0.01f, 10.0f, "%.2f"))
            {
                meshScale = glm::clamp(meshScale, 0.001f, 100.0f);
            }

            ImGui::Spacing();

            if (ImGui::Button("Reset", ImVec2(-1, 0)))
            {
                meshPosition = glm::vec3(0.0f);
                meshRotation = glm::vec3(0.0f);
                meshScale = 1.0f;
            }

            if (ImGui::Button("Fit Camera", ImVec2(-1, 0)))
            {
                // Use cached bounds from when mesh was loaded
                camera->fitToBounds(meshBounds);
            }
        }

        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        {
            float itemWidth = ImGui::GetContentRegionAvail().x - 50.0f;

            float dist = camera->getDistance();
            float minDist = camera->getMinDistance();
            float maxDist = camera->getMaxDistance();

            ImGui::Text("Zoom");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            if (ImGui::SliderFloat("##Zoom", &dist, minDist, maxDist, "%.1f", ImGuiSliderFlags_Logarithmic))
            {
                camera->setDistance(dist);
            }

            // Zoom buttons
            if (ImGui::Button("+", ImVec2(itemWidth / 2 - 2, 0)))
            {
                camera->setDistance(dist * 0.9f);
            }
            ImGui::SameLine();
            if (ImGui::Button("-", ImVec2(itemWidth / 2 - 2, 0)))
            {
                camera->setDistance(dist * 1.1f);
            }
        }
    }
}
