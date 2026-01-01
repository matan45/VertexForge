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
        // Preview cleanup is handled in draw() when window closes
        // to ensure cleanup happens before ImGui tries to render freed resources
    }

    void MeshPreviewWindow::draw()
    {
        // Handle cleanup when window is closing - must happen BEFORE any ImGui rendering
        if (!isOpen)
        {
            if (!previewCleanedUp)
            {
                // Cancel any ongoing async loading
                if (loadingProgress.isLoading())
                {
                    services::events::preview::CancelMeshLoadingCommand cancelCmd;
                    cancelCmd.instanceId = services::PreviewInstanceId(this);
                    events::EventDispatcher::instance().execute(cancelCmd);
                }

                services::events::preview::CleanUpMeshPreviewCommand cleanupCmd;
                cleanupCmd.instanceId = services::PreviewInstanceId(this);
                events::EventDispatcher::instance().execute(cleanupCmd);

                previewCleanedUp = true;
            }
            return;
        }

        // Initialize on first draw
        if (needsInit)
        {
            initRenderer();
            needsInit = false;
        }

        // Update async loading state
        updateAsyncLoading();

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

                // Show loading indicator or viewport
                if (loadingProgress.isLoading())
                {
                    drawLoadingIndicator(viewportSize.x, viewportSize.y);
                }
                else
                {
                    drawViewport(viewportSize.x, viewportSize.y);
                }

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

        // Start async loading
        services::events::preview::LoadPreviewMeshAsyncCommand loadCmd;
        loadCmd.instanceId = services::PreviewInstanceId(this);
        loadCmd.meshPath = meshPath;
        events::EventDispatcher::instance().execute(loadCmd);

        // Initialize loading state
        loadingProgress.state = services::LoadingState::Pending;
        loadingProgress.progress = 0.0f;
        loadingProgress.statusMessage = "Starting load...";
    }

    void MeshPreviewWindow::updateAsyncLoading()
    {
        if (!loadingProgress.isLoading())
        {
            return;
        }

        // Query current loading progress
        services::events::preview::GetMeshLoadingProgressQuery progressQuery;
        progressQuery.instanceId = services::PreviewInstanceId(this);
        loadingProgress = events::EventDispatcher::instance().query(progressQuery);

        // Check if loading completed
        if (loadingProgress.state == services::LoadingState::Complete)
        {
            onLoadingComplete();
        }
        else if (loadingProgress.state == services::LoadingState::Error)
        {
            vfLogError("Failed to load mesh: {}", loadingProgress.errorMessage);
        }
    }

    void MeshPreviewWindow::onLoadingComplete()
    {
        // Get mesh bounds
        services::events::preview::GetPreviewMeshBoundsQuery boundsQuery;
        boundsQuery.instanceId = services::PreviewInstanceId(this);
        meshBounds = events::EventDispatcher::instance().query(boundsQuery);
        camera->fitToBounds(meshBounds);

        // Get submesh info
        services::events::preview::GetPreviewMeshSubMeshInfoQuery subMeshQuery;
        subMeshQuery.instanceId = services::PreviewInstanceId(this);
        subMeshes = events::EventDispatcher::instance().query(subMeshQuery);

        // Get LOD info
        services::events::preview::GetPreviewMeshLODInfoQuery lodQuery;
        lodQuery.instanceId = services::PreviewInstanceId(this);
        lodLevels = events::EventDispatcher::instance().query(lodQuery);
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

    void MeshPreviewWindow::drawLoadingIndicator(float width, float height)
    {
        ImVec2 windowPos = ImGui::GetCursorScreenPos();
        ImVec2 windowSize(width, height);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Semi-transparent dark overlay
        drawList->AddRectFilled(
            windowPos,
            ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y),
            IM_COL32(20, 20, 20, 200)
        );

        // Center content
        float contentWidth = 250.0f;
        float contentHeight = 120.0f;
        float centerX = windowPos.x + (windowSize.x - contentWidth) * 0.5f;
        float centerY = windowPos.y + (windowSize.y - contentHeight) * 0.5f;

        // Spinner animation
        float time = static_cast<float>(ImGui::GetTime());
        float spinnerRadius = 20.0f;
        float spinnerThickness = 4.0f;
        ImVec2 spinnerCenter(centerX + contentWidth * 0.5f, centerY + 30.0f);

        // Draw spinner arc
        int numSegments = 30;
        float startAngle = time * 3.0f;
        float arcLength = 3.14159f * 1.2f; // About 216 degrees

        ImU32 spinnerColor = IM_COL32(100, 150, 255, 255);
        for (int i = 0; i < numSegments; ++i)
        {
            float t1 = static_cast<float>(i) / static_cast<float>(numSegments);
            float t2 = static_cast<float>(i + 1) / static_cast<float>(numSegments);
            float angle1 = startAngle + t1 * arcLength;
            float angle2 = startAngle + t2 * arcLength;

            // Fade alpha along the arc
            int alpha = static_cast<int>(255 * (1.0f - t1 * 0.7f));
            ImU32 segColor = IM_COL32(100, 150, 255, alpha);

            ImVec2 p1(spinnerCenter.x + cosf(angle1) * spinnerRadius,
                      spinnerCenter.y + sinf(angle1) * spinnerRadius);
            ImVec2 p2(spinnerCenter.x + cosf(angle2) * spinnerRadius,
                      spinnerCenter.y + sinf(angle2) * spinnerRadius);

            drawList->AddLine(p1, p2, segColor, spinnerThickness);
        }

        // Status message
        const char* statusText = loadingProgress.statusMessage.c_str();
        ImVec2 textSize = ImGui::CalcTextSize(statusText);
        ImVec2 textPos(centerX + (contentWidth - textSize.x) * 0.5f, centerY + 60.0f);
        drawList->AddText(textPos, IM_COL32(200, 200, 200, 255), statusText);

        // Progress bar
        float progressBarY = centerY + 85.0f;
        float progressBarHeight = 8.0f;
        float progressBarWidth = contentWidth - 20.0f;
        float progressBarX = centerX + 10.0f;

        // Background
        drawList->AddRectFilled(
            ImVec2(progressBarX, progressBarY),
            ImVec2(progressBarX + progressBarWidth, progressBarY + progressBarHeight),
            IM_COL32(60, 60, 60, 255),
            4.0f
        );

        // Progress fill
        float fillWidth = progressBarWidth * loadingProgress.progress;
        if (fillWidth > 0)
        {
            drawList->AddRectFilled(
                ImVec2(progressBarX, progressBarY),
                ImVec2(progressBarX + fillWidth, progressBarY + progressBarHeight),
                spinnerColor,
                4.0f
            );
        }

        // Progress percentage text
        char progressText[16];
        snprintf(progressText, sizeof(progressText), "%.0f%%", loadingProgress.progress * 100.0f);
        ImVec2 progressTextSize = ImGui::CalcTextSize(progressText);
        ImVec2 progressTextPos(centerX + (contentWidth - progressTextSize.x) * 0.5f, progressBarY + 15.0f);
        drawList->AddText(progressTextPos, IM_COL32(150, 150, 150, 255), progressText);
    }
}
