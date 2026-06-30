#include "print/Log.hpp"
#include "MeshPreviewWindow.hpp"
#include "PreviewInputHandler.hpp"
#include "PreviewToolbar.hpp"
#include "../../camera/OrbitCamera.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "MeshSocketWriter.hpp"
#include "resource/ConvexDecompositionSidecar.hpp"
#include "imgui.h"
#include <imgui_internal.h>
#include "ImGuizmo.h"
#include "events/EventDispatcher.hpp"
#include "events/lifecycle/AssetLifecycleEvents.hpp"
#include "events/physics/PhysicsEvents.hpp"
#include "events/render/PreviewEvents.hpp"
#include "events/physics/SocketEvents.hpp"
#include "events/project/SceneEvents.hpp"
#include <IconsFontAwesome6.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <map>
#include <cstring>

namespace windows
{
    namespace
    {
        std::string normalizePathForCompare(const std::string& path)
        {
            if (path.empty())
                return {};

            std::error_code ec;
            std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
            if (ec)
            {
                ec.clear();
                normalized = std::filesystem::absolute(path, ec);
            }

            std::string result = ec ? path : normalized.string();
            std::replace(result.begin(), result.end(), '\\', '/');
            std::transform(result.begin(), result.end(), result.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return result;
        }

        const char* submeshNameOrFallback(const std::vector<services::SubMeshInfo>& subMeshes,
                                          int submeshIndex)
        {
            if (submeshIndex < 0 || static_cast<size_t>(submeshIndex) >= subMeshes.size())
                return "Unknown";
            return subMeshes[static_cast<size_t>(submeshIndex)].name.c_str();
        }
    }

    MeshPreviewWindow::MeshPreviewWindow(const std::string& meshFilePath)
        : meshPath(meshFilePath)
          , camera(std::make_unique<editor::OrbitCamera>())
    {
        std::filesystem::path path(meshFilePath);
        windowTitle = "Mesh Preview: " + path.filename().string();
    }

    MeshPreviewWindow::~MeshPreviewWindow()
    {
        cancelConvexRegen.store(true);
        if (convexRegenFuture.valid())
            convexRegenFuture.wait();
    }

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

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse | maximizer.windowFlags()))
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
        if (!streamHandle)
        {
            hasSkeleton = false;
            return;
        }

        if (!streamHandle->hasSkeletonData())
        {
            // Static mesh (no skeleton). It may still carry a SOK2 socket block
            // (VK-1427) — load those so the static-mesh socket editor can author them.
            hasSkeleton = false;
            boneNames.clear();
            sockets.clear();
            if (streamHandle->hasSocketData())
            {
                resource::SkeletonData tmp;
                if (streamHandle->readSockets(tmp))
                {
                    sockets = tmp.sockets;
                }
            }
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

        // Don't let the orbit camera consume mouse input while the socket gizmo is
        // being dragged (static-mesh authoring) — otherwise the camera fights the
        // gizmo. ImGuizmo::IsUsing() reflects the state from the previous frame's
        // Manipulate() call, which is exactly the drag we want to protect.
        if (!ImGuizmo::IsUsing())
        {
            editor::preview::PreviewInputHandler::handleInput(camera.get(), isDraggingOrbit, isDraggingPan);
        }

        sendEnvironmentParams();

        glm::mat4 model = glm::mat4_cast(meshPreviewRotation);

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

            // VK-1427 Phase 3: ImGuizmo handle over the rendered image, anchored to
            // the image's screen rect (captured from the item we just submitted).
            drawSocketGizmo();
        }
    }

    void MeshPreviewWindow::drawSocketGizmo()
    {
        // Static-mesh authoring only, and only for a valid selection.
        if (hasSkeleton) return;
        if (selectedSocketIndex < 0 || selectedSocketIndex >= static_cast<int>(sockets.size())) return;

        // Rect of the ImGui::Image() submitted immediately before this call.
        ImVec2 imgMin = ImGui::GetItemRectMin();
        ImVec2 imgSz = ImGui::GetItemRectSize();
        if (imgSz.x <= 0.0f || imgSz.y <= 0.0f) return;

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(imgMin.x, imgMin.y, imgSz.x, imgSz.y);

        // Undo the Vulkan Y-flip for ImGuizmo (expects OpenGL-style projection),
        // matching ViewPortGizmo.
        glm::mat4 view = camera->getViewMatrix();
        glm::mat4 proj = camera->getProjectionMatrix();
        proj[1][1] *= -1.0f;

        // The mesh may be shown under a preview rotation (turntable buttons), so the
        // socket's world transform is model * localOffset. Manipulate in world, then
        // strip the model rotation back off so the stored offset stays mesh-local
        // (when meshPreviewRotation is identity this is byte-for-byte the old path).
        auto& socket = sockets[selectedSocketIndex];
        glm::mat4 model = glm::mat4_cast(meshPreviewRotation);
        glm::mat4 objectMatrix = model * socket.getLocalOffsetMatrix();

        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                                 socketGizmoOp, socketGizmoMode, glm::value_ptr(objectMatrix)))
        {
            glm::mat4 localMatrix = glm::inverse(model) * objectMatrix;
            float translation[3], rotation[3], scale[3];
            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(localMatrix),
                                                  translation, rotation, scale);
            socket.localPosition = glm::vec3(translation[0], translation[1], translation[2]);
            socket.localRotation = glm::quat(glm::radians(glm::vec3(rotation[0], rotation[1], rotation[2])));
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

        if (ImGui::CollapsingHeader("Orientation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("Rotate mesh (preview only)");

            // Pre-multiply so each press rotates the mesh about a fixed world axis,
            // which reads more intuitively than local-axis turns. Preview-only: this
            // never touches the saved mesh or the socket offsets (see drawSocketGizmo).
            auto rotateMesh = [this](const glm::vec3& axis, float deg)
            {
                meshPreviewRotation = glm::normalize(
                    glm::angleAxis(glm::radians(deg), axis) * meshPreviewRotation);
            };

            float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

            if (ImGui::Button("X -90", ImVec2(btnW, 0))) rotateMesh(glm::vec3(1, 0, 0), -90.0f);
            ImGui::SameLine();
            if (ImGui::Button("X +90", ImVec2(btnW, 0))) rotateMesh(glm::vec3(1, 0, 0), 90.0f);

            if (ImGui::Button("Y -90", ImVec2(btnW, 0))) rotateMesh(glm::vec3(0, 1, 0), -90.0f);
            ImGui::SameLine();
            if (ImGui::Button("Y +90", ImVec2(btnW, 0))) rotateMesh(glm::vec3(0, 1, 0), 90.0f);

            if (ImGui::Button("Z -90", ImVec2(btnW, 0))) rotateMesh(glm::vec3(0, 0, 1), -90.0f);
            ImGui::SameLine();
            if (ImGui::Button("Z +90", ImVec2(btnW, 0))) rotateMesh(glm::vec3(0, 0, 1), 90.0f);

            if (ImGui::Button("Reset Orientation", ImVec2(-1, 0)))
            {
                meshPreviewRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            }
        }

        ImGui::Separator();
        drawColliderPanel();

        ImGui::Separator();
        drawSocketPanel();
    }

    void MeshPreviewWindow::drawColliderPanel()
    {
        pollConvexRegenerationResult();

        if (!ImGui::CollapsingHeader("Collider", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        ImGui::TextWrapped("Convex data is stored in one .vfCollider sidecar next to this .vfMesh.");
        ImGui::TextWrapped("Changing it affects every entity that uses this mesh.");

        const auto sidecarPath = resource::ConvexDecompositionSidecar::sidecarPathForMesh(meshPath);
        ImGui::TextDisabled("Sidecar: %s", sidecarPath.filename().string().c_str());

        drawColliderSidecarSummary();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Target:");
        if (selectedSubMesh < 0)
        {
            ImGui::TextDisabled("  All submeshes");
        }
        else
        {
            ImGui::TextDisabled("  Submesh %d: %s", selectedSubMesh,
                                submeshNameOrFallback(subMeshes, selectedSubMesh));
        }

        drawConvexRegenerationSettings();

        const bool running = convexRegenFuture.valid()
            && convexRegenFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;

        if (running)
        {
            ImGui::ProgressBar(std::clamp(convexRegenProgress.load(), 0.0f, 1.0f), ImVec2(-1.0f, 0.0f));
            if (ImGui::Button("Cancel Regeneration"))
                cancelConvexRegen.store(true);
        }
        else
        {
            const bool canRegenerate = !meshPath.empty();
            if (!canRegenerate)
                ImGui::BeginDisabled();

            const char* buttonLabel = selectedSubMesh < 0
                ? "Regenerate Convex Decomposition"
                : "Regenerate Selected Submesh";
            if (ImGui::Button(buttonLabel))
            {
                cancelConvexRegen.store(false);
                convexRegenProgress.store(0.0f);
                convexRegenStatus.clear();
                activeConvexRegenSubmesh = selectedSubMesh;
                convexRebuiltEntityCount = 0;

                const std::string targetMeshPath = meshPath;
                const int32_t targetSubmesh = selectedSubMesh;
                const auto config = convexRegenConfig;
                convexRegenFuture = std::async(std::launch::async,
                    [this, targetMeshPath, targetSubmesh, config]()
                    {
                        return types::ConvexDecompositionRegenerator::regenerate(
                            targetMeshPath,
                            targetSubmesh,
                            config,
                            [this](float progress, std::string_view)
                            {
                                convexRegenProgress.store(progress);
                            },
                            &cancelConvexRegen);
                    });
            }

            if (!canRegenerate)
            {
                ImGui::EndDisabled();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "No mesh path resolved");
            }
        }

        if (!convexRegenStatus.empty())
        {
            ImGui::TextWrapped("%s", convexRegenStatus.c_str());
        }
    }

    bool MeshPreviewWindow::drawConvexRegenerationSettings()
    {
        bool changed = false;

        const char* presetNames[] = {"Fast", "Balanced", "Quality", "Custom"};
        int presetIndex = static_cast<int>(convexRegenConfig.vhacdPreset);
        if (ImGui::Combo("Quality Preset##MeshPreviewConvexRegen", &presetIndex, presetNames, IM_ARRAYSIZE(presetNames)))
        {
            convexRegenConfig.vhacdPreset = static_cast<importConfig::VHACDPreset>(presetIndex);
            changed = true;
        }

        const bool isCustom = convexRegenConfig.vhacdPreset == importConfig::VHACDPreset::Custom;
        if (!isCustom)
            ImGui::BeginDisabled();

        int maxHulls = static_cast<int>(convexRegenConfig.maxConvexHulls);
        if (ImGui::SliderInt("Max Hulls##MeshPreviewConvexRegen", &maxHulls, 1, 64))
        {
            convexRegenConfig.maxConvexHulls = static_cast<uint32_t>(maxHulls);
            changed = true;
        }

        int maxVerts = static_cast<int>(convexRegenConfig.maxVerticesPerHull);
        if (ImGui::SliderInt("Max Vertices Per Hull##MeshPreviewConvexRegen", &maxVerts, 8, 256))
        {
            convexRegenConfig.maxVerticesPerHull = static_cast<uint32_t>(maxVerts);
            changed = true;
        }

        if (ImGui::TreeNode("Advanced Parameters##MeshPreviewConvexRegen"))
        {
            int resolution = static_cast<int>(convexRegenConfig.vhacdResolution);
            if (ImGui::SliderInt("Resolution##MeshPreviewConvexRegen", &resolution, 10000, 500000))
            {
                convexRegenConfig.vhacdResolution = static_cast<uint32_t>(resolution);
                changed = true;
            }

            float minVolumeError = convexRegenConfig.minVolumePercentError;
            if (ImGui::SliderFloat("Min Volume Error %%##MeshPreviewConvexRegen", &minVolumeError, 0.1f, 10.0f, "%.1f"))
            {
                convexRegenConfig.minVolumePercentError = minVolumeError;
                changed = true;
            }

            int recursionDepth = static_cast<int>(convexRegenConfig.maxRecursionDepth);
            if (ImGui::SliderInt("Max Recursion Depth##MeshPreviewConvexRegen", &recursionDepth, 4, 16))
            {
                convexRegenConfig.maxRecursionDepth = static_cast<uint32_t>(recursionDepth);
                changed = true;
            }

            if (ImGui::Checkbox("Shrink Wrap##MeshPreviewConvexRegen", &convexRegenConfig.shrinkWrap))
                changed = true;

            ImGui::TreePop();
        }

        if (!isCustom)
            ImGui::EndDisabled();

        return changed;
    }

    void MeshPreviewWindow::drawColliderSidecarSummary()
    {
        std::vector<resource::ConvexDecompositionSidecarEntry> entries;
        if (!resource::ConvexDecompositionSidecar::load(meshPath, entries))
        {
            ImGui::TextDisabled("No generated collider sidecar");
            return;
        }

        uint32_t hullCount = 0;
        for (const auto& entry : entries)
            hullCount += static_cast<uint32_t>(entry.data.hulls.size());

        ImGui::Text("Generated collider: %zu submeshes, %u hulls", entries.size(), hullCount);

        if (selectedSubMesh >= 0)
        {
            const auto it = std::find_if(entries.begin(), entries.end(),
                [this](const auto& entry)
                {
                    return entry.submeshIndex == static_cast<uint32_t>(selectedSubMesh);
                });

            if (it == entries.end() || !it->data.isValid())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                                   "Selected submesh has no generated convex data");
            }
            else
            {
                ImGui::TextDisabled("Selected submesh: %zu hulls", it->data.hulls.size());
            }
        }
    }

    void MeshPreviewWindow::pollConvexRegenerationResult()
    {
        if (!convexRegenFuture.valid())
            return;

        if (convexRegenFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
            return;

        types::ConvexRegenerationResult result;
        try
        {
            result = convexRegenFuture.get();
        }
        catch (const std::exception& e)
        {
            convexRegenStatus = std::string("Convex decomposition failed: ") + e.what();
            return;
        }

        if (!result.success)
        {
            convexRegenStatus = "Convex decomposition failed: " + result.message;
            return;
        }

        convexRegenProgress.store(1.0f);

        auto& dispatcher = events::EventDispatcher::instance();
        events::lifecycle::AssetReleaseReadyNotification releaseNotification;
        releaseNotification.path = meshPath;
        releaseNotification.type = resource::AssetType::Mesh;
        dispatcher.publish(releaseNotification);

        convexRebuiltEntityCount = rebuildPhysicsBodiesUsingMesh(activeConvexRegenSubmesh);

        convexRegenStatus = result.message + " (" + std::to_string(result.hullCount) + " hulls)";
        if (convexRebuiltEntityCount > 0)
        {
            convexRegenStatus += ", rebuilt " + std::to_string(convexRebuiltEntityCount) + " live physics ";
            convexRegenStatus += convexRebuiltEntityCount == 1 ? "body" : "bodies";
        }
    }

    uint32_t MeshPreviewWindow::rebuildPhysicsBodiesUsingMesh(int32_t regeneratedSubmesh) const
    {
        const std::string targetPath = normalizePathForCompare(meshPath);
        if (targetPath.empty())
            return 0;

        uint32_t rebuiltCount = 0;
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::GetEntitiesWithComponentQuery query;
        query.componentType = services::ComponentTypeId::Collider;

        std::vector<services::EntityHandle> entities;
        try
        {
            entities = dispatcher.query(query);
        }
        catch (const std::exception&)
        {
            return 0;
        }

        for (const auto& entity : entities)
        {
            try
            {
                events::scene::GetColliderDataQuery colliderQuery;
                colliderQuery.entity = entity;
                auto colliderOpt = dispatcher.query(colliderQuery);
                if (!colliderOpt.has_value())
                    continue;

                if (colliderOpt->shape != types::ColliderShape::ConvexMesh &&
                    colliderOpt->shape != types::ColliderShape::TriangleMesh)
                {
                    continue;
                }

                std::string entityMeshPath;
                if (colliderOpt->meshRef.isValid())
                {
                    entityMeshPath = colliderOpt->meshRef.resolve();
                }
                else
                {
                    events::scene::GetMeshDataQuery meshQuery;
                    meshQuery.entity = entity;
                    auto meshOpt = dispatcher.query(meshQuery);
                    if (meshOpt.has_value() && meshOpt->meshRef.isValid())
                        entityMeshPath = meshOpt->meshRef.resolve();
                }

                if (normalizePathForCompare(entityMeshPath) != targetPath)
                    continue;

                if (regeneratedSubmesh >= 0 &&
                    colliderOpt->submeshIndex >= 0 &&
                    colliderOpt->submeshIndex != regeneratedSubmesh)
                {
                    continue;
                }

                events::physics::HasRigidBodyQuery hasBodyQuery;
                hasBodyQuery.entity = entity;
                if (!dispatcher.query(hasBodyQuery))
                    continue;

                events::physics::CreatePhysicsBodyCommand rebuildCmd;
                rebuildCmd.entity = entity;
                rebuildCmd.rebuild = true;
                if (dispatcher.execute(rebuildCmd))
                    ++rebuiltCount;
            }
            catch (const std::exception&)
            {
                continue;
            }
        }

        return rebuiltCount;
    }

    void MeshPreviewWindow::drawSocketPanel()
    {
        if (!ImGui::CollapsingHeader("Sockets", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        if (hasSkeleton)
        {
            // Skeletal meshes: sockets are bound to bones and authored in the
            // Animation Preview. Keep this panel read-only to avoid regressing
            // that flow (a socket without a valid bone makes no sense here).
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
            return;
        }

        // Static mesh: full authoring. Sockets are mesh-local (no bone), so
        // targetBoneName stays empty and boneIndex == -1.
        drawStaticSocketEditor();
    }

    void MeshPreviewWindow::drawStaticSocketEditor()
    {
        // --- Create socket ---
        if (ImGui::CollapsingHeader("Create Socket", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            ImGui::InputText("Name", newSocketName, sizeof(newSocketName));

            float rot[3] = {newSocketEulerDeg.x, newSocketEulerDeg.y, newSocketEulerDeg.z};
            if (ImGui::DragFloat3("Rotation##staticSocketNew", rot, 0.5f))
            {
                newSocketEulerDeg = glm::vec3(rot[0], rot[1], rot[2]);
            }

            bool canCreate = std::strlen(newSocketName) > 0;
            if (canCreate)
            {
                for (const auto& existing : sockets)
                {
                    if (existing.name == newSocketName)
                    {
                        canCreate = false;
                        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Name already exists");
                        break;
                    }
                }
            }

            if (!canCreate) ImGui::BeginDisabled();
            bool addClicked = ImGui::Button("Add Socket");
            if (!canCreate) ImGui::EndDisabled();

            if (addClicked)
            {
                animator::SocketDefinition newSocket;
                newSocket.name = newSocketName;
                newSocket.targetBoneName.clear();
                newSocket.boneIndex = -1;
                newSocket.localRotation = glm::quat(glm::radians(newSocketEulerDeg));
                sockets.push_back(newSocket);

                newSocketName[0] = '\0';
                newSocketEulerDeg = glm::vec3(0.0f);
                selectedSocketIndex = static_cast<int>(sockets.size()) - 1;
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::Spacing();
        ImGui::Text("Socket List (%zu)", sockets.size());
        ImGui::Separator();

        if (sockets.empty())
        {
            ImGui::TextDisabled("No sockets defined");
            drawSocketSaveButton();
            return;
        }

        // --- Socket list (selectable) ---
        for (int i = 0; i < static_cast<int>(sockets.size()); ++i)
        {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (selectedSocketIndex == i)
                flags |= ImGuiTreeNodeFlags_Selected;

            bool nodeOpen = ImGui::TreeNodeEx(sockets[i].name.c_str(), flags);
            if (ImGui::IsItemClicked())
                selectedSocketIndex = i;
            if (nodeOpen)
                ImGui::TreePop();
        }

        // --- Selected-socket editor ---
        if (selectedSocketIndex >= 0 && selectedSocketIndex < static_cast<int>(sockets.size()))
        {
            auto& socket = sockets[selectedSocketIndex];

            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Socket Properties", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent(10.0f);

                // Editable name. Seed a local buffer from the socket and write back on
                // change so an existing socket can be renamed (not just at creation).
                char nameBuf[128];
                std::strncpy(nameBuf, socket.name.c_str(), sizeof(nameBuf) - 1);
                nameBuf[sizeof(nameBuf) - 1] = '\0';
                if (ImGui::InputText("Name##staticSocketEdit", nameBuf, sizeof(nameBuf)))
                {
                    socket.name = nameBuf;
                }
                // Non-blocking warning if the (edited) name now collides with another
                // socket — duplicate names make Socket::getPosition(name) ambiguous.
                for (int j = 0; j < static_cast<int>(sockets.size()); ++j)
                {
                    if (j != selectedSocketIndex && sockets[j].name == socket.name)
                    {
                        ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), "Duplicate name");
                        break;
                    }
                }

                float pos[3] = {socket.localPosition.x, socket.localPosition.y, socket.localPosition.z};
                if (ImGui::DragFloat3("Position", pos, 0.01f))
                {
                    socket.localPosition = glm::vec3(pos[0], pos[1], pos[2]);
                }

                glm::vec3 eulerDeg = glm::degrees(glm::eulerAngles(socket.localRotation));
                float rot[3] = {eulerDeg.x, eulerDeg.y, eulerDeg.z};
                if (ImGui::DragFloat3("Rotation##staticSocketEdit", rot, 0.5f))
                {
                    socket.localRotation = glm::quat(glm::radians(glm::vec3(rot[0], rot[1], rot[2])));
                }

                ImGui::Spacing();
                ImGui::TextDisabled("Gizmo:");
                ImGui::SameLine();
                if (ImGui::RadioButton("Move", socketGizmoOp == ImGuizmo::TRANSLATE))
                    socketGizmoOp = ImGuizmo::TRANSLATE;
                ImGui::SameLine();
                if (ImGui::RadioButton("Rotate", socketGizmoOp == ImGuizmo::ROTATE))
                    socketGizmoOp = ImGuizmo::ROTATE;

                ImGui::Unindent(10.0f);
            }

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Delete Socket"))
            {
                sockets.erase(sockets.begin() + selectedSocketIndex);
                selectedSocketIndex = -1;
            }
            ImGui::PopStyleColor(2);
        }

        ImGui::Spacing();
        ImGui::Separator();
        drawSocketSaveButton();
    }

    void MeshPreviewWindow::drawSocketSaveButton()
    {
        if (socketSaveMessageTimer > 0.0f)
        {
            socketSaveMessageTimer -= ImGui::GetIO().DeltaTime;
        }

        bool canSave = !meshPath.empty() && !sockets.empty();

        if (!canSave) ImGui::BeginDisabled();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.5f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.65f, 0.2f, 1.0f));
        if (ImGui::Button("Save Sockets to Mesh"))
        {
            socketSaveSuccess = types::MeshSocketWriter::saveSocketsToMesh(meshPath, sockets);
            socketSaveMessageTimer = 3.0f;
            if (socketSaveSuccess)
            {
                events::socket::SocketDataSavedNotification notif;
                notif.meshPath = meshPath;
                events::EventDispatcher::instance().publish(notif);
            }
        }
        ImGui::PopStyleColor(2);

        if (!canSave) ImGui::EndDisabled();

        if (sockets.empty())
        {
            ImGui::TextDisabled("No sockets to save");
        }

        if (socketSaveMessageTimer > 0.0f)
        {
            if (socketSaveSuccess)
            {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Sockets saved!");
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Save failed! Check log.");
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
