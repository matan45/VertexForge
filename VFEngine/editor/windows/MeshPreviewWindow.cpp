#include "MeshPreviewWindow.hpp"
#include "../camera/OrbitCamera.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "imgui.h"
#include "print/EditorLogger.hpp"
#include "events/EventDispatcher.hpp"
#include "events/PreviewEvents.hpp"
#include <filesystem>
#include <glm/gtc/quaternion.hpp>

namespace windows
{
    MeshPreviewWindow::MeshPreviewWindow(const std::string& meshFilePath)
        : meshPath(meshFilePath)
          , camera(std::make_unique<editor::OrbitCamera>())
    {
        std::filesystem::path path(meshFilePath);
        windowTitle = "Mesh Preview: " + path.filename().string();
    }

    MeshPreviewWindow::~MeshPreviewWindow() = default;

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

        // Load skeleton and socket data from the .vfMesh file
        loadSkeletonData();
    }

    void MeshPreviewWindow::loadSkeletonData()
    {
        auto streamHandle = resource::MeshStreamResource::openStream(meshPath);
        if (!streamHandle || !streamHandle->hasSkeletonData())
        {
            hasSkeleton = false;
            return;
        }

        resource::SkeletonData skeleton;
        if (!streamHandle->readSkeleton(skeleton))
        {
            hasSkeleton = false;
            return;
        }

        hasSkeleton = true;

        // Extract bone names
        boneNames.clear();
        boneNames.reserve(skeleton.bones.size());
        for (const auto& bone : skeleton.bones)
        {
            boneNames.push_back(bone.name);
        }

        // Load existing sockets
        sockets = skeleton.sockets;
    }

    void MeshPreviewWindow::handlePreviewInput()
    {
        bool isHovered = ImGui::IsWindowHovered();

        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            isDraggingPreview = true;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            isDraggingPreview = false;
        }

        if (!isHovered) return;

        ImGuiIO& io = ImGui::GetIO();

        // Scroll to zoom
        if (io.MouseWheel != 0.0f)
        {
            float zoomFactor = 1.0f - io.MouseWheel * camera->zoomSensitivity * 0.1f;
            camera->setDistance(camera->distance * zoomFactor);
            camera->updateMatrices();
        }

        // Left mouse drag to orbit
        if (isDraggingPreview && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            ImVec2 delta = io.MouseDelta;

            if (delta.x != 0.0f || delta.y != 0.0f)
            {
                camera->yaw += delta.x * camera->orbitSensitivity;
                camera->pitch -= delta.y * camera->orbitSensitivity;
                camera->pitch = glm::clamp(camera->pitch, -89.0f, 89.0f);
                camera->updateMatrices();
            }
        }
    }

    void MeshPreviewWindow::drawViewport(float width, float height)
    {
        if (width <= 0 || height <= 0)
        {
            return;
        }
        camera->setAspectRatio(width / height);

        handlePreviewInput();

        glm::mat4 model = glm::mat4(1.0f);

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

            ImGui::Spacing();

            if (ImGui::Button("Fit to Mesh", ImVec2(-1, 0)))
            {
                camera->fitToBounds(meshBounds);
            }
        }

        // Socket panel for skeletal meshes
        if (hasSkeleton)
        {
            ImGui::Separator();
            drawSocketPanel();
        }
    }

    void MeshPreviewWindow::drawSocketPanel()
    {
        if (ImGui::CollapsingHeader("Sockets", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Bones: %zu", boneNames.size());

            if (sockets.empty())
            {
                ImGui::TextDisabled("No sockets defined");
                ImGui::TextDisabled("(Edit in Animation Preview)");
                return;
            }

            ImGui::Text("Sockets: %zu", sockets.size());
            ImGui::Separator();

            for (const auto& socket : sockets)
            {
                if (ImGui::TreeNode(socket.name.c_str()))
                {
                    ImGui::TextDisabled("Bone: %s", socket.targetBoneName.c_str());
                    ImGui::TextDisabled("Pos: %.2f, %.2f, %.2f",
                                        socket.localPosition.x, socket.localPosition.y, socket.localPosition.z);
                    glm::vec3 euler = glm::degrees(glm::eulerAngles(socket.localRotation));
                    ImGui::TextDisabled("Rot: %.1f, %.1f, %.1f", euler.x, euler.y, euler.z);
                    ImGui::TextDisabled("Scl: %.2f, %.2f, %.2f",
                                        socket.localScale.x, socket.localScale.y, socket.localScale.z);
                    ImGui::TreePop();
                }
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
