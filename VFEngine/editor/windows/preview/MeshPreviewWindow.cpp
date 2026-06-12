#include "print/Log.hpp"
#include "MeshPreviewWindow.hpp"
#include "PreviewInputHandler.hpp"
#include "PreviewToolbar.hpp"
#include "../../camera/OrbitCamera.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "imgui.h"
#include <imgui_internal.h>
#include "events/EventDispatcher.hpp"
#include "events/render/PreviewEvents.hpp"
#include <IconsFontAwesome6.h>
#include <filesystem>
#include <map>

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
        if (!isOpen)
        {
            if (!previewCleanedUp)
            {
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

        updateAsyncLoading();

        if (initialSize.x <= 0.0f)
        {
            initialSize = editor::preview::initialWindowSize("MeshPreview", ImVec2(1000, 700));
        }
        ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
        maximizer.preBegin();

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse))
        {
            if (isOpen)
            {
                maximizer.drawButton();

                static float panelWidth = 200.0f;
                const float splitterThickness = 5.0f;
                ImVec2 contentSize = ImGui::GetContentRegionAvail();
                panelWidth = std::clamp(panelWidth, 150.0f,
                                        std::max(150.0f, contentSize.x - 300.0f - splitterThickness));
                float viewportWidth = contentSize.x - panelWidth - splitterThickness;

                ImGui::BeginChild("SubMeshPanel", ImVec2(panelWidth, contentSize.y), true);
                drawSubMeshPanel();
                ImGui::EndChild();

                ImGui::SameLine(0.0f, 0.0f);
                editor::preview::splitterV("##meshSplit", splitterThickness, &panelWidth,
                                           &viewportWidth, 150.0f, 300.0f, contentSize.y);
                ImGui::SameLine(0.0f, 0.0f);

                ImGui::BeginChild("ViewportPanel", ImVec2(viewportWidth, contentSize.y), true,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                ImVec2 viewportSize = ImGui::GetContentRegionAvail();

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

        if (!isOpen && !sizeSaved)
        {
            editor::preview::rememberWindowSize("MeshPreview", maximizer.effectiveSize());
            sizeSaved = true;
        }
    }

    void MeshPreviewWindow::initRenderer()
    {
        services::events::preview::InitMeshPreviewCommand initCmd;
        initCmd.instanceId = services::PreviewInstanceId(this);
        events::EventDispatcher::instance().execute(initCmd);

        services::events::preview::LoadPreviewMeshAsyncCommand loadCmd;
        loadCmd.instanceId = services::PreviewInstanceId(this);
        loadCmd.meshPath = meshPath;
        events::EventDispatcher::instance().execute(loadCmd);

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

        services::events::preview::GetMeshLoadingProgressQuery progressQuery;
        progressQuery.instanceId = services::PreviewInstanceId(this);
        loadingProgress = events::EventDispatcher::instance().query(progressQuery);

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
        services::events::preview::GetPreviewMeshBoundsQuery boundsQuery;
        boundsQuery.instanceId = services::PreviewInstanceId(this);
        meshBounds = events::EventDispatcher::instance().query(boundsQuery);
        camera->fitToBounds(meshBounds);

        services::events::preview::GetPreviewMeshSubMeshInfoQuery subMeshQuery;
        subMeshQuery.instanceId = services::PreviewInstanceId(this);
        subMeshes = events::EventDispatcher::instance().query(subMeshQuery);

        services::events::preview::GetPreviewMeshLODInfoQuery lodQuery;
        lodQuery.instanceId = services::PreviewInstanceId(this);
        lodLevels = events::EventDispatcher::instance().query(lodQuery);

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

        boneNames.clear();
        boneNames.reserve(skeleton.bones.size());
        for (const auto& bone : skeleton.bones)
        {
            boneNames.push_back(bone.name);
        }

        sockets = skeleton.sockets;
    }

    void MeshPreviewWindow::sendEnvironmentParams()
    {
        services::PreviewEnvironmentParams envParams;
        envParams.backgroundMode = static_cast<uint8_t>(environment.backgroundMode);
        envParams.backgroundColor = environment.backgroundColor;
        envParams.gradientTopColor = environment.gradientTopColor;
        envParams.gradientBottomColor = environment.gradientBottomColor;
        envParams.showGrid = environment.showGrid;
        envParams.lightingMode = static_cast<uint8_t>(environment.lightingMode);
        envParams.lightingIntensity = environment.lightingIntensity;

        services::events::preview::SetPreviewEnvironmentCommand envCmd;
        envCmd.instanceId = services::PreviewInstanceId(this);
        envCmd.params = envParams;
        events::EventDispatcher::instance().execute(envCmd);
    }

    void MeshPreviewWindow::drawViewport(float width, float height)
    {
        if (width <= 0 || height <= 0)
        {
            return;
        }

        // Toolbar at top of viewport
        editor::preview::PreviewToolbar::draw(environment, camera.get(), &meshBounds);
        ImGui::Separator();

        // Recalculate viewport size after toolbar
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        width = viewportSize.x;
        height = viewportSize.y;
        if (width <= 0 || height <= 0) return;

        camera->setAspectRatio(width / height);

        editor::preview::PreviewInputHandler::handleInput(camera.get(), isDraggingOrbit, isDraggingPan);

        sendEnvironmentParams();

        glm::mat4 model = glm::mat4(1.0f);

        services::MeshPreviewParams meshParams;
        meshParams.modelMatrix = model;
        meshParams.highlightedSubMesh = selectedSubMesh;
        meshParams.forceLODLevel = selectedLOD;
        meshParams.wireframeMode = wireframeMode;
        meshParams.showBoundingBox = showBoundingBox;
        meshParams.materialOverrideMode = materialOverrideMode;

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

        // "All" option with total count
        bool allSelected = (selectedSubMesh == -1);
        char allLabel[64];
        snprintf(allLabel, sizeof(allLabel), "All (%zu)", subMeshes.size());
        if (ImGui::Selectable(allLabel, allSelected))
        {
            selectedSubMesh = -1;
        }

        ImGui::Separator();

        // Group submeshes by name
        std::map<std::string, std::vector<int>> groups;
        for (size_t i = 0; i < subMeshes.size(); ++i)
        {
            groups[subMeshes[i].name].push_back(static_cast<int>(i));
        }

        for (const auto& [groupName, indices] : groups)
        {
            if (indices.size() == 1)
            {
                // Single item - render flat
                int idx = indices[0];
                bool isSelected = (selectedSubMesh == idx);
                ImGui::PushID(idx);
                if (ImGui::Selectable(groupName.c_str(), isSelected))
                {
                    selectedSubMesh = idx;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::Text("Vertices: %u", subMeshes[idx].vertexCount);
                    ImGui::Text("Triangles: %u", subMeshes[idx].indexCount / 3);
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
            else
            {
                // Group with count - collapsible
                char groupLabel[256];
                snprintf(groupLabel, sizeof(groupLabel), "%s (%zu)", groupName.c_str(), indices.size());

                bool anySelected = false;
                for (int idx : indices)
                {
                    if (selectedSubMesh == idx) { anySelected = true; break; }
                }

                ImGui::PushID(groupName.c_str());
                ImGuiTreeNodeFlags flags = anySelected ? ImGuiTreeNodeFlags_Selected : 0;
                bool open = ImGui::TreeNodeEx(groupLabel, flags);

                // Click on group header selects first instance
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                {
                    selectedSubMesh = indices[0];
                }

                if (open)
                {
                    for (int idx : indices)
                    {
                        bool isSelected = (selectedSubMesh == idx);
                        char itemLabel[64];
                        snprintf(itemLabel, sizeof(itemLabel), "#%d", idx);
                        ImGui::PushID(idx);
                        if (ImGui::Selectable(itemLabel, isSelected))
                        {
                            selectedSubMesh = idx;
                        }
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::BeginTooltip();
                            ImGui::Text("Vertices: %u", subMeshes[idx].vertexCount);
                            ImGui::Text("Triangles: %u", subMeshes[idx].indexCount / 3);
                            ImGui::EndTooltip();
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }

        ImGui::Separator();

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

            if (ImGui::Button("Fit to Mesh", ImVec2(-1, 0)))
            {
                camera->fitToBounds(meshBounds);
            }
        }

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

        drawList->AddRectFilled(
            windowPos,
            ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y),
            IM_COL32(20, 20, 20, 200)
        );

        float contentWidth = 250.0f;
        float contentHeight = 120.0f;
        float centerX = windowPos.x + (windowSize.x - contentWidth) * 0.5f;
        float centerY = windowPos.y + (windowSize.y - contentHeight) * 0.5f;

        float time = static_cast<float>(ImGui::GetTime());
        float spinnerRadius = 20.0f;
        float spinnerThickness = 4.0f;
        ImVec2 spinnerCenter(centerX + contentWidth * 0.5f, centerY + 30.0f);

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

            int alpha = static_cast<int>(255 * (1.0f - t1 * 0.7f));
            ImU32 segColor = IM_COL32(100, 150, 255, alpha);

            ImVec2 p1(spinnerCenter.x + cosf(angle1) * spinnerRadius,
                      spinnerCenter.y + sinf(angle1) * spinnerRadius);
            ImVec2 p2(spinnerCenter.x + cosf(angle2) * spinnerRadius,
                      spinnerCenter.y + sinf(angle2) * spinnerRadius);

            drawList->AddLine(p1, p2, segColor, spinnerThickness);
        }

        const char* statusText = loadingProgress.statusMessage.c_str();
        ImVec2 textSize = ImGui::CalcTextSize(statusText);
        ImVec2 textPos(centerX + (contentWidth - textSize.x) * 0.5f, centerY + 60.0f);
        drawList->AddText(textPos, IM_COL32(200, 200, 200, 255), statusText);

        float progressBarY = centerY + 85.0f;
        float progressBarHeight = 8.0f;
        float progressBarWidth = contentWidth - 20.0f;
        float progressBarX = centerX + 10.0f;

        drawList->AddRectFilled(
            ImVec2(progressBarX, progressBarY),
            ImVec2(progressBarX + progressBarWidth, progressBarY + progressBarHeight),
            IM_COL32(60, 60, 60, 255),
            4.0f
        );

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

        char progressText[16];
        snprintf(progressText, sizeof(progressText), "%.0f%%", loadingProgress.progress * 100.0f);
        ImVec2 progressTextSize = ImGui::CalcTextSize(progressText);
        ImVec2 progressTextPos(centerX + (contentWidth - progressTextSize.x) * 0.5f, progressBarY + 15.0f);
        drawList->AddText(progressTextPos, IM_COL32(150, 150, 150, 255), progressText);
    }
}
