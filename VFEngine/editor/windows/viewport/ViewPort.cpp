#include "ViewPort.hpp"
#include "events/EventDispatcher.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/editor/SculptModeEvents.hpp"
#include "events/terrain/TerrainRaycastEvents.hpp"
#include "events/terrain/BrushEvents.hpp"
#include "events/terrain/SplineTerrainEvents.hpp"
#include "events/terrain/PaintModeEvents.hpp"
#include "events/terrain/PaintBrushEvents.hpp"
#include "events/terrain/HoleModeEvents.hpp"
#include "events/terrain/HoleBrushEvents.hpp"
#include "events/terrain/TerrainStrokeEvents.hpp"
#include "events/terrain/CaveModeEvents.hpp"
#include "events/terrain/CaveBrushEvents.hpp"
#include "events/vegetation/VegetationBrushEvents.hpp"
#include "events/meshbrush/MeshBrushEvents.hpp"
#include "events/foliage/FoliageBrushEvents.hpp"
#include "events/ui/UIPickEvents.hpp"
#include "events/input/InputEvents.hpp"
#include "events/audio/AudioEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "events/render/MaterialEvents.hpp"
#include "events/physics/PhysicsEvents.hpp"
#include "asset/AssetRef.hpp"
#include "time/Timer.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "data/DTOs.hpp"
#include "data/EntityConversion.hpp"
#include "../../dragdrop/DragDropManager.hpp"
#include <imgui.h>
#include "ImGuizmo.h"
#include <algorithm>
#include <filesystem>

namespace windows
{
    ViewPort::ViewPort()
        : editorCamera(std::make_unique<editor::EditorCamera>())
    {
    }

    void ViewPort::draw()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (ImGui::Begin("ViewPort"))
        {
            bool isFocused = ImGui::IsWindowFocused();
            bool isHovered = ImGui::IsWindowHovered();
            bool isPlayMode = dispatcher.query(events::editor::IsPlayModeQuery{});

            // Maintain/exit an active RMB look capture every frame regardless of hover: the OS
            // cursor is locked (GLFW_CURSOR_DISABLED) while captured, so IsWindowHovered is
            // unreliable and gating exit on it could strand the capture on (VK-1428).
            updateCameraLook(isPlayMode);

            // Allow camera input when hovered, OR while a look capture is active (the locked OS
            // cursor makes IsWindowHovered unreliable, but WASD fly must keep working while the
            // user holds RMB to look). Entry into look still requires hover (handleCameraInput).
            if (isFocused && !isPlayMode && (isHovered || cameraLookActive))
            {
                handleCameraInput();
            }

            ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
            if (viewportPanelSize.x > 0 && viewportPanelSize.y > 0)
            {
                editorCamera->setAspectRatio(viewportPanelSize.x / viewportPanelSize.y);
            }

            float aspectRatio = viewportPanelSize.x / viewportPanelSize.y;
            CameraState camera = getActiveCameraState(isPlayMode, aspectRatio);
            updateRendererCameras(camera);

            ImVec2 viewportPos = ImGui::GetCursorScreenPos();

            glm::vec2 vp(viewportPos.x, viewportPos.y);
            glm::vec2 vs(viewportPanelSize.x, viewportPanelSize.y);

            if (isPlayMode)
            {
                events::render::SetUIViewportOffsetCommand offsetCmd;
                offsetCmd.offset = vp;
                offsetCmd.panelSize = vs;
                dispatcher.execute(offsetCmd);
            }

            // Always publish — zero panelSize outside play mode clears stale state in subscribers
            // (e.g. WindowStateServiceImpl, which serves script-side viewport-relative input queries).
            events::render::PlayViewportRectChangedNotification rectNotif;
            if (isPlayMode)
            {
                rectNotif.offset = vp;
                rectNotif.panelSize = vs;
            }
            dispatcher.publish(rectNotif);

            updateBrushCursors(vp, vs);

            events::render::GetViewportTextureQuery query;
            auto texture = dispatcher.query(query);
            if (texture.isValid())
            {
                ImGui::Image(texture.imguiDescriptorSet, ImVec2{viewportPanelSize.x, viewportPanelSize.y});
                handleAssetDrop(vp, vs);
            }

            // Play-mode feedback: outline the viewport so it is obvious the editor is running
            // the game. Green while playing, amber while paused (VK Play/Pause/Stop Phase 1).
            if (isPlayMode)
            {
                bool isPaused = dispatcher.query(events::editor::IsEditorPausedQuery{});
                ImU32 borderColor = isPaused ? IM_COL32(230, 180, 60, 255) : IM_COL32(80, 200, 120, 255);
                ImGui::GetWindowDrawList()->AddRect(
                    viewportPos,
                    ImVec2(viewportPos.x + viewportPanelSize.x, viewportPos.y + viewportPanelSize.y),
                    borderColor, 0.0f, 0, 3.0f);
            }

            overlay.draw(gizmo);
            // VK-1595: after overlay.draw so the streaming panel's own Begin/End nests the same
            // way, and before the gizmo so ImGuizmo still owns the top of the draw order.
            streamingOverlay.draw();
            gizmo.draw(*editorCamera);
            const bool attenuationViewportAvailable =
                texture.isValid() && vs.x > 0.0f && vs.y > 0.0f;
            audioAttenuationGizmo.draw(
                *editorCamera, picker, vp, vs, attenuationViewportAvailable, isPlayMode,
                isHovered, isFocused, cameraLookActive);
            const bool attenuationGizmoConsumesMouse = audioAttenuationGizmo.wantsMouseCapture();

            if (!isPlayMode)
            {
                picker.updateBillboardScreenPositions(*editorCamera, vp, vs);
                picker.updateMeshPickData();
                drawSelectedUIOutline(vp, vs);
            }

            handleEntityPicking(isPlayMode, vp, vs, attenuationGizmoConsumesMouse);
            selector.update(picker, *editorCamera, isPlayMode, vp, vs,
                            audioAttenuationGizmo.isDragging());
            handleSculptBrush();
            handlePaintBrush();
            handleHoleBrush();
            handleCaveBrush();
            handleVegetationBrush();
            handleMeshBrush();
            handleFoliageBrush();
            handleSplineTool();
        }
        ImGui::End();
    }

    bool ViewPort::tryGetGameCameraState(CameraState& state, float aspectRatio)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        auto primaryCameraOpt = dispatcher.query(events::scene::GetPrimaryCameraQuery{});
        if (!primaryCameraOpt.has_value()) return false;

        auto primaryCamera = *primaryCameraOpt;
        events::scene::GetCameraDataQuery cameraQuery;
        cameraQuery.entity = primaryCamera;
        if (!dispatcher.query(cameraQuery).has_value()) return false;

        events::scene::GetWorldTransformQuery transformQuery;
        transformQuery.entity = primaryCamera;
        auto transformOpt = dispatcher.query(transformQuery);
        if (!transformOpt.has_value()) return false;

        auto& transform = *transformOpt;
        auto enttEntity = services::internal::fromHandle(primaryCamera);
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::CameraComponent>(enttEntity)) return false;

        if (!registry.all_of<components::TransformComponent>(enttEntity)) return false;

        auto& camComp = registry.get<components::CameraComponent>(enttEntity);
        // VK-1416: skip the engine recompute when a script owns the camera matrices.
        if (!camComp.viewMatrixOverride)
        {
            camComp.aspectRatio = aspectRatio;
            camComp.updateProjectionMatrix();

            // Derive the view from the camera's LOCAL rotation (camera Y·X·Z order), not from the world
            // transform's decomposed Euler. `transform` here is a TransformData DTO from
            // GetWorldTransformQuery, whose rotation is the world matrix decomposed via
            // extractEulerAngleXYZ — that extraction's gimbal singularity is on the yaw axis at ±90°,
            // which flips a yawing fixed-pitch camera to the sky. updateViewMatrixFromWorldEye keeps any
            // parent orientation while interpreting the local rotation in camera order (VK-1350).
            const auto& localTransform = registry.get<components::TransformComponent>(enttEntity);
            if (registry.all_of<components::WorldTransformComponent>(enttEntity))
            {
                const auto& worldMatrix = registry.get<components::WorldTransformComponent>(enttEntity).worldMatrix;
                camComp.updateViewMatrixFromWorldEye(worldMatrix, localTransform);
            }
            else
            {
                camComp.updateViewMatrix(localTransform.position, localTransform.rotation);
            }
        }

        state.viewMatrix = camComp.viewMatrix;
        state.projectionMatrix = camComp.projectionMatrix;
        state.position = camComp.viewMatrixOverride
            ? glm::vec3(glm::inverse(camComp.viewMatrix)[3])
            : transform.position;

        // Forward from the resolved camera world orientation (inverse of the view), so a child camera
        // under a rotating parent reports the correct listener direction too.
        glm::mat4 cameraWorld = glm::inverse(camComp.viewMatrix);
        state.forward = glm::normalize(-glm::vec3(cameraWorld[2]));
        return true;
    }

    CameraState ViewPort::getActiveCameraState(bool isPlayMode, float aspectRatio)
    {
        CameraState state;
        if (isPlayMode && tryGetGameCameraState(state, aspectRatio))
        {
            return state;
        }

        state.viewMatrix = editorCamera->getViewMatrix();
        state.projectionMatrix = editorCamera->getProjectionMatrix();
        state.position = editorCamera->position;
        state.forward = editorCamera->getForwardDirection();
        return state;
    }

    void ViewPort::updateRendererCameras(const CameraState& camera)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::render::UpdateIBLCameraCommand cameraCmd;
        cameraCmd.viewMatrix = camera.viewMatrix;
        cameraCmd.projectionMatrix = camera.projectionMatrix;
        dispatcher.execute(cameraCmd);

        events::render::UpdateMeshCameraCommand meshCameraCmd;
        meshCameraCmd.viewMatrix = camera.viewMatrix;
        meshCameraCmd.projectionMatrix = camera.projectionMatrix;
        meshCameraCmd.cameraPosition = camera.position;
        meshCameraCmd.time = static_cast<float>(engineTime::Timer::getElapsedTime());
        dispatcher.execute(meshCameraCmd);

        events::render::CameraPositionUpdatedNotification camPosNotif;
        camPosNotif.position = camera.position;
        dispatcher.publish(camPosNotif);

        events::audio::SetListenerPositionCommand listenerCmd;
        listenerCmd.position = camera.position;
        listenerCmd.forward = camera.forward;
        listenerCmd.up = glm::vec3(0.0f, 1.0f, 0.0f);
        dispatcher.execute(listenerCmd);
    }

    glm::vec3 ViewPort::computeDropPosition(glm::vec2 mousePos, glm::vec2 viewportPos, glm::vec2 viewportSize)
    {
        if (viewportSize.x > 0.0f && viewportSize.y > 0.0f)
        {
            math::Ray ray = picker.screenToWorldRay(*editorCamera, mousePos, viewportPos, viewportSize);

            events::physics::RaycastQuery rayQuery;
            rayQuery.origin = ray.origin;
            rayQuery.direction = ray.direction;
            rayQuery.maxDistance = 10000.0f;
            auto hit = events::EventDispatcher::instance().query(rayQuery);
            if (hit.hit)
            {
                return hit.point;
            }
        }

        return editorCamera->position + editorCamera->getForwardDirection() * 5.0f;
    }

    void ViewPort::spawnPrefabAt(const std::string& path, const glm::vec3& dropPos)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::LoadPrefabCommand loadCmd;
        loadCmd.filePath = path;
        loadCmd.parent = std::nullopt;
        auto result = dispatcher.execute(loadCmd);
        if (!result.has_value())
        {
            return;
        }

        events::scene::GetTransformQuery transformQuery;
        transformQuery.entity = *result;
        auto prefabTransform = dispatcher.query(transformQuery);

        if (prefabTransform.has_value())
        {
            services::TransformData transform;
            transform.position = dropPos;
            transform.rotation = prefabTransform->rotation;
            transform.scale = prefabTransform->scale;

            events::scene::SetTransformCommand transformCmd;
            transformCmd.entity = *result;
            transformCmd.transform = transform;
            dispatcher.execute(transformCmd);
        }

        events::scene::SelectEntityCommand selectCmd;
        selectCmd.entity = *result;
        dispatcher.execute(selectCmd);
    }

    void ViewPort::handleAssetDrop(glm::vec2 viewportPos, glm::vec2 viewportSize)
    {
        if (!ImGui::BeginDragDropTarget())
        {
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DND_CONTENT_BROWSER))
        {
            // Copy out of the singleton before dispatching — the spawn commands
            // below can trigger UI refreshes that touch the drag state.
            std::vector<std::string> dragPaths = DragDropManager::instance().getDragPaths();
            DragDropManager::instance().endDrag();

            ImVec2 mouse = ImGui::GetMousePos();
            glm::vec2 mousePos(mouse.x, mouse.y);
            glm::vec3 dropPos = computeDropPosition(mousePos, viewportPos, viewportSize);

            auto spawnComponentEntity = [&](const std::filesystem::path& fsPath,
                                            auto&& addAndConfigure)
            {
                events::scene::CreateEntityCommand createCmd;
                createCmd.name = fsPath.stem().string();
                services::EntityHandle entity = dispatcher.execute(createCmd);

                addAndConfigure(entity);

                events::scene::SetTransformCommand transformCmd;
                transformCmd.entity = entity;
                transformCmd.transform.position = dropPos;
                dispatcher.execute(transformCmd);

                events::scene::SelectEntityCommand selectCmd;
                selectCmd.entity = entity;
                dispatcher.execute(selectCmd);
            };

            for (const auto& path : dragPaths)
            {
                std::filesystem::path fsPath(path);
                std::string ext = fsPath.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                if (ext == ".vfterrain")
                {
                    events::terrain::BeginTerrainLoadCommand loadCmd;
                    loadCmd.path = path;
                    dispatcher.execute(loadCmd);
                }
                else if (ext == ".vfprefab")
                {
                    spawnPrefabAt(path, dropPos);
                }
                else if (ext == ".vfmesh")
                {
                    spawnComponentEntity(fsPath, [&](services::EntityHandle entity)
                    {
                        events::scene::AddMeshComponentCommand addCmd;
                        addCmd.entity = entity;
                        dispatcher.execute(addCmd);

                        events::scene::SetMeshDataCommand meshCmd;
                        meshCmd.entity = entity;
                        meshCmd.meshData.meshRef = asset::AssetRef::fromPath(path);
                        dispatcher.execute(meshCmd);

                        events::material::AddMaterialComponentCommand matCmd;
                        matCmd.entity = entity;
                        dispatcher.execute(matCmd);
                    });
                }
                else if (ext == ".vfvfx")
                {
                    spawnComponentEntity(fsPath, [&](services::EntityHandle entity)
                    {
                        events::scene::AddVFXComponentCommand addCmd;
                        addCmd.entity = entity;
                        dispatcher.execute(addCmd);

                        events::scene::SetVFXDataCommand vfxCmd;
                        vfxCmd.entity = entity;
                        vfxCmd.vfxData.vfxRef = asset::AssetRef::fromPath(path);
                        dispatcher.execute(vfxCmd);
                    });
                }
                else if (ext == ".vfvfxsequence")
                {
                    spawnComponentEntity(fsPath, [&](services::EntityHandle entity)
                    {
                        events::scene::AddVFXSequenceComponentCommand addCmd;
                        addCmd.entity = entity;
                        dispatcher.execute(addCmd);

                        events::scene::SetVFXSequenceDataCommand seqCmd;
                        seqCmd.entity = entity;
                        seqCmd.vfxSequenceData.sequenceRef = asset::AssetRef::fromPath(path);
                        dispatcher.execute(seqCmd);
                    });
                }
                else if (ext == ".vfaudio")
                {
                    spawnComponentEntity(fsPath, [&](services::EntityHandle entity)
                    {
                        events::scene::AddAudioSource3DComponentCommand addCmd;
                        addCmd.entity = entity;
                        dispatcher.execute(addCmd);

                        events::scene::SetAudioSource3DDataCommand audioCmd;
                        audioCmd.entity = entity;
                        audioCmd.audioData.audioRef = asset::AssetRef::fromPath(path);
                        dispatcher.execute(audioCmd);
                    });
                }
                else if (ext == ".vfmat" || ext == ".vfmatinstance")
                {
                    auto target = picker.pickMeshAt(*editorCamera, mousePos, viewportPos, viewportSize);
                    if (target.has_value())
                    {
                        events::material::HasMaterialComponentQuery hasMatQuery;
                        hasMatQuery.entity = *target;
                        if (!dispatcher.query(hasMatQuery))
                        {
                            events::material::AddMaterialComponentCommand addCmd;
                            addCmd.entity = *target;
                            dispatcher.execute(addCmd);
                        }

                        events::material::SetDefaultMaterialCommand matCmd;
                        matCmd.entity = *target;
                        matCmd.materialPath = path;
                        dispatcher.execute(matCmd);
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    void ViewPort::handleEntityPicking(bool isPlayMode, glm::vec2 viewportPos,
                                       glm::vec2 viewportSize, bool customGizmoConsumesMouse)
    {
        if (isPlayMode) return;
        if (customGizmoConsumesMouse) return;

        auto& sculptDispatcher = events::EventDispatcher::instance();
        if (sculptDispatcher.query(events::sculpt::IsSculptModeActiveQuery{})) return;
        if (sculptDispatcher.query(events::paint::IsPaintModeActiveQuery{})) return;
        if (sculptDispatcher.query(events::hole::IsHoleModeActiveQuery{})) return;
        if (sculptDispatcher.query(events::vegetationBrush::IsVegetationBrushModeActiveQuery{})) return;
        bool meshBrushActive = sculptDispatcher.query(events::meshBrush::IsMeshBrushModeActiveQuery{});
        if (meshBrushActive && !ImGui::GetIO().KeyCtrl) return;
        bool foliageBrushActive = sculptDispatcher.query(events::foliageBrush::IsFoliageBrushModeActiveQuery{});
        if (foliageBrushActive && !ImGui::GetIO().KeyCtrl) return;

        if (!ImGui::IsWindowHovered()) return;
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) return;
        if (ImGuizmo::IsUsing() || ImGuizmo::IsOver()) return;

        ImVec2 mousePos = ImGui::GetMousePos();
        glm::vec2 mp(mousePos.x, mousePos.y);

        auto picked = picker.pickBillboardAt(mp);
        if (!picked.has_value())
        {
            picked = picker.pickUIAt(*editorCamera, mp, viewportPos, viewportSize);
        }
        if (!picked.has_value())
        {
            picked = picker.pickMeshAt(*editorCamera, mp, viewportPos, viewportSize);
        }

        if (picked.has_value())
        {
            selector.handleEntityClick(*picked);
        }
        else
        {
            // Empty space: candidate for a drag-marquee or, released without a
            // drag, an empty-click clear (resolved in ViewPortSelection::update).
            selector.beginMarqueeCandidate(mp);
        }
    }

    void ViewPort::drawSelectedUIOutline(glm::vec2 viewportPos, glm::vec2 viewportSize)
    {
        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) return;

        auto& dispatcher = events::EventDispatcher::instance();
        // VK-1490: every selected UI entity keeps its quad outline in a
        // multi-selection (meshes get the silhouette outline instead).
        auto selectedEntities = dispatcher.query(events::scene::GetSelectedEntitiesQuery{});
        if (selectedEntities.empty()) return;

        glm::mat4 viewProj = editorCamera->getProjectionMatrix() * editorCamera->getViewMatrix();

        // No Y flip — EditorCamera's projection already flips Y for Vulkan
        auto project = [&](const glm::vec4& clip) {
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            return ImVec2((ndc.x * 0.5f + 0.5f) * viewportSize.x + viewportPos.x,
                          (ndc.y * 0.5f + 0.5f) * viewportSize.y + viewportPos.y);
        };

        for (const auto& selected : selectedEntities)
        {
            events::ui::GetUIEntityWorldQuadQuery quadQuery;
            quadQuery.entity = selected;
            auto quad = dispatcher.query(quadQuery);
            if (!quad.has_value()) continue;

            glm::vec4 clipCorners[4];
            for (int i = 0; i < 4; ++i)
            {
                clipCorners[i] = viewProj * glm::vec4(quad->corners[i], 1.0f);
            }

            // Corners behind the camera (clip.w <= 0) project to garbage — clip each
            // edge against the near plane and draw the surviving polygon instead
            constexpr float nearW = 1e-4f;
            ImVec2 points[8];
            int pointCount = 0;
            for (int i = 0; i < 4; ++i)
            {
                const glm::vec4& a = clipCorners[i];
                const glm::vec4& b = clipCorners[(i + 1) % 4];
                bool aIn = a.w > nearW;
                bool bIn = b.w > nearW;
                if (!aIn && !bIn) continue;

                if (aIn) points[pointCount++] = project(a);
                if (aIn != bIn)
                {
                    float tEdge = (nearW - a.w) / (b.w - a.w);
                    points[pointCount++] = project(a + (b - a) * tEdge);
                }
            }
            if (pointCount < 2) continue; // fully behind the camera

            ImGui::GetWindowDrawList()->AddPolyline(points, pointCount, IM_COL32(255, 161, 0, 255),
                                                    ImDrawFlags_Closed, 2.0f);
        }
    }

    void ViewPort::handleCameraInput()
    {
        float dt = static_cast<float>(engineTime::Timer::getDeltaTime());

        bool forward = ImGui::IsKeyDown(ImGuiKey_W);
        bool backward = ImGui::IsKeyDown(ImGuiKey_S);
        bool left = ImGui::IsKeyDown(ImGuiKey_A);
        bool right = ImGui::IsKeyDown(ImGuiKey_D);
        bool up = ImGui::IsKeyDown(ImGuiKey_E);
        bool down = ImGui::IsKeyDown(ImGuiKey_Q);
        bool sprint = ImGui::IsKeyDown(ImGuiKey_LeftShift);

        if (forward || backward || left || right || up || down)
        {
            editorCamera->processKeyboardInput(dt, forward, backward, left, right, up, down, sprint);
        }

        // Enter camera look on RMB press. Capture is done OS-side (GLFW_CURSOR_DISABLED + raw
        // motion) so rotation uses true relative motion free of FIFO coalescing/clamping; the
        // delta is applied — and capture released — in updateCameraLook(), which runs every frame
        // regardless of hover. Entry stays gated on focus+hover (here) so look only starts when
        // the viewport is genuinely under the cursor. VK-1428.
        if (!cameraLookActive && ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            events::input::SetRelativeMouseModeCommand cmd;
            cmd.enabled = true;
            events::EventDispatcher::instance().execute(cmd);
            cameraLookActive = true;
        }
    }

    void ViewPort::updateCameraLook(bool isPlayMode)
    {
        if (!cameraLookActive)
        {
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        // Exit on RMB release, focus loss, or entering play mode — release the OS cursor and
        // restore its pre-capture position.
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) || !ImGui::IsWindowFocused() || isPlayMode)
        {
            events::input::SetRelativeMouseModeCommand cmd;
            cmd.enabled = false;
            dispatcher.execute(cmd);
            cameraLookActive = false;
            return;
        }

        glm::vec2 delta = dispatcher.query(events::input::GetRelativeMouseDeltaQuery{});
        if (delta.x != 0.0f || delta.y != 0.0f)
        {
            editorCamera->processMouseMovement(delta.x, delta.y);
        }
    }

    void ViewPort::updateBrushCursors(glm::vec2 viewportPos, glm::vec2 viewportSize)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        bool sculptActive = dispatcher.query(events::sculpt::IsSculptModeActiveQuery{});
        bool paintActive = dispatcher.query(events::paint::IsPaintModeActiveQuery{});
        bool holeActive = dispatcher.query(events::hole::IsHoleModeActiveQuery{});
        bool caveActive = dispatcher.query(events::cave::IsCaveModeActiveQuery{});
        bool vegActive = dispatcher.query(events::vegetationBrush::IsVegetationBrushModeActiveQuery{});
        bool meshBrushActive = dispatcher.query(events::meshBrush::IsMeshBrushModeActiveQuery{});
        bool foliageBrushActive = dispatcher.query(events::foliageBrush::IsFoliageBrushModeActiveQuery{});

        bool splineActive = dispatcher.query(events::splineTerrain::IsSplineModeActiveQuery{});

        bool anyActive = sculptActive || paintActive || holeActive || caveActive || vegActive || meshBrushActive || foliageBrushActive || splineActive;

        if (!anyActive || !ImGui::IsWindowHovered())
        {
            if (!anyActive)
            {
                dispatcher.execute(events::terrainRaycast::ClearCursorCommand{});
            }
            return;
        }

        if (sculptActive) updateSculptCursorUV(viewportPos, viewportSize);
        else if (paintActive) updatePaintCursorUV(viewportPos, viewportSize);
        else if (holeActive) updateHoleCursorUV(viewportPos, viewportSize);
        else if (caveActive) sendCursorUV(viewportPos, viewportSize);
        else if (vegActive) updateVegetationCursorUV(viewportPos, viewportSize);
        else if (meshBrushActive) updateMeshBrushCursorUV(viewportPos, viewportSize);
        else if (foliageBrushActive) updateFoliageBrushCursorUV(viewportPos, viewportSize);
        else if (splineActive) sendCursorUV(viewportPos, viewportSize);
    }

    void ViewPort::sendCursorUV(glm::vec2 viewportPos, glm::vec2 viewportSize)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        ImVec2 mousePos = ImGui::GetMousePos();
        glm::vec2 uv = glm::clamp((glm::vec2(mousePos.x, mousePos.y) - viewportPos) / viewportSize,
                                   glm::vec2(0.0f), glm::vec2(1.0f));
        events::terrainRaycast::SetCursorPositionCommand cmd;
        cmd.cursorUV = uv;
        events::EventDispatcher::instance().execute(cmd);
    }

    void ViewPort::updateSculptCursorUV(glm::vec2 viewportPos, glm::vec2 viewportSize)
    {
        sendCursorUV(viewportPos, viewportSize);
    }

    void ViewPort::handleSculptBrush()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (!dispatcher.query(events::sculpt::IsSculptModeActiveQuery{}) || !ImGui::IsWindowHovered()) {
            // VK-1615: dragging off the viewport (or leaving sculpt mode) must still close
            // the stroke, or its undo entry is silently discarded.
            if (sculptDragging)
                dispatcher.execute(events::terrain::FinalizeTerrainStrokeCommand{});
            sculptDragging = false;
            return;
        }
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            auto hitResult = dispatcher.query(events::terrainRaycast::GetTerrainHitQuery{});
            if (hitResult.hit) {
                events::brush::ApplyBrushCommand applyCmd;
                applyCmd.worldPosition = hitResult.position;
                applyCmd.deltaTime = ImGui::GetIO().DeltaTime;
                applyCmd.invert = ImGui::GetIO().KeyShift;
                applyCmd.isFirstApplication = !sculptDragging;
                dispatcher.execute(applyCmd);
                sculptDragging = true;
            }
        } else {
            if (sculptDragging)
                dispatcher.execute(events::terrain::FinalizeTerrainStrokeCommand{});
            sculptDragging = false;
        }
    }

    void ViewPort::updatePaintCursorUV(glm::vec2 viewportPos, glm::vec2 viewportSize)
    {
        sendCursorUV(viewportPos, viewportSize);
    }

    void ViewPort::handlePaintBrush()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (!dispatcher.query(events::paint::IsPaintModeActiveQuery{}) || !ImGui::IsWindowHovered()) {
            if (paintDragging)
                dispatcher.execute(events::terrain::FinalizeTerrainStrokeCommand{});
            paintDragging = false;
            return;
        }
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            auto hitResult = dispatcher.query(events::terrainRaycast::GetTerrainHitQuery{});
            if (hitResult.hit) {
                events::paintBrush::ApplyPaintBrushCommand applyCmd;
                applyCmd.worldPosition = hitResult.position;
                applyCmd.deltaTime = ImGui::GetIO().DeltaTime;
                applyCmd.invert = ImGui::GetIO().KeyShift;
                applyCmd.isFirstApplication = !paintDragging;
                dispatcher.execute(applyCmd);
                paintDragging = true;
            }
        } else {
            if (paintDragging)
                dispatcher.execute(events::terrain::FinalizeTerrainStrokeCommand{});
            paintDragging = false;
        }
    }

    void ViewPort::updateHoleCursorUV(glm::vec2 viewportPos, glm::vec2 viewportSize)
    {
        sendCursorUV(viewportPos, viewportSize);
    }

    void ViewPort::handleHoleBrush()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (!dispatcher.query(events::hole::IsHoleModeActiveQuery{}) || !ImGui::IsWindowHovered()) {
            if (holeDragging)
                dispatcher.execute(events::terrain::FinalizeTerrainStrokeCommand{});
            holeDragging = false;
            return;
        }
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            auto hitResult = dispatcher.query(events::terrainRaycast::GetTerrainHitQuery{});
            if (hitResult.hit) {
                events::holeBrush::ApplyHoleBrushCommand applyCmd;
                applyCmd.worldPosition = hitResult.position;
                applyCmd.erase = ImGui::GetIO().KeyShift;
                // VK-1615: hole strokes previously had no drag concept at all, so every
                // frame of a drag was an independent edit with nothing to undo.
                applyCmd.isFirstApplication = !holeDragging;
                dispatcher.execute(applyCmd);
                holeDragging = true;
            }
        } else {
            if (holeDragging)
                dispatcher.execute(events::terrain::FinalizeTerrainStrokeCommand{});
            holeDragging = false;
        }
    }

    void ViewPort::handleCaveBrush()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (!dispatcher.query(events::cave::IsCaveModeActiveQuery{}) || !ImGui::IsWindowHovered())
        {
            // VK-1615: this path used to reset the latch without finalizing, so dragging
            // off the viewport silently dropped the cave stroke's undo entry (mesh,
            // foliage and vegetation brushes all finalize here).
            if (caveDragging)
            {
                events::caveBrush::FinalizeCaveBrushCommand finalizeCmd;
                dispatcher.execute(finalizeCmd);
            }
            caveDragging = false;
            return;
        }
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            auto hitResult = dispatcher.query(events::terrainRaycast::GetTerrainHitQuery{});
            if (hitResult.hit)
            {
                // Carve at the cursor hit so the carve matches the brush-overlay ghost
                // (drawn at the hit position). The carve only affects originally-solid
                // voxels (CaveBrushApplicator's checkOriginalSolid guard), so a surface
                // click scoops inward without wasting the brush on air — no inward
                // surface-normal offset hack needed.
                events::caveBrush::ApplyCaveBrushCommand applyCmd;
                applyCmd.worldPosition = hitResult.position;
                applyCmd.deltaTime = ImGui::GetIO().DeltaTime;
                applyCmd.invert = ImGui::GetIO().KeyShift;
                applyCmd.isFirstApplication = !caveDragging;
                dispatcher.execute(applyCmd);
                caveDragging = true;
            }
        }
        else
        {
            if (caveDragging)
            {
                // Mouse released — finalize: punch holes, rebuild physics
                events::caveBrush::FinalizeCaveBrushCommand finalizeCmd;
                dispatcher.execute(finalizeCmd);
            }
            caveDragging = false;
        }
    }

    void ViewPort::updateVegetationCursorUV(glm::vec2 viewportPos, glm::vec2 viewportSize)
    {
        sendCursorUV(viewportPos, viewportSize);
    }

    void ViewPort::handleVegetationBrush()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (!dispatcher.query(events::vegetationBrush::IsVegetationBrushModeActiveQuery{}) || !ImGui::IsWindowHovered()) {
            if (vegetationDragging) {
                events::vegetationBrush::FinalizeVegetationBrushCommand finalizeCmd;
                dispatcher.execute(finalizeCmd);
            }
            vegetationDragging = false;
            return;
        }
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            auto hitResult = dispatcher.query(events::terrainRaycast::GetTerrainHitQuery{});
            if (hitResult.hit) {
                events::vegetationBrush::ApplyVegetationBrushCommand applyCmd;
                applyCmd.worldPosition = hitResult.position;
                applyCmd.surfaceNormal = hitResult.normal;
                applyCmd.deltaTime = ImGui::GetIO().DeltaTime;
                applyCmd.isFirstApplication = !vegetationDragging;
                dispatcher.execute(applyCmd);
                vegetationDragging = true;
            }
        } else {
            if (vegetationDragging) {
                // Mouse released — record one undo entry for the whole stroke
                events::vegetationBrush::FinalizeVegetationBrushCommand finalizeCmd;
                dispatcher.execute(finalizeCmd);
            }
            vegetationDragging = false;
        }
    }

    void ViewPort::updateMeshBrushCursorUV(glm::vec2 viewportPos, glm::vec2 viewportSize)
    {
        sendCursorUV(viewportPos, viewportSize);
    }

    void ViewPort::handleMeshBrush()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (!dispatcher.query(events::meshBrush::IsMeshBrushModeActiveQuery{}) || !ImGui::IsWindowHovered() || ImGui::GetIO().KeyCtrl) {
            if (meshBrushDragging) {
                events::meshBrush::FinalizeMeshBrushCommand finalizeCmd;
                dispatcher.execute(finalizeCmd);
            }
            meshBrushDragging = false;
            return;
        }
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            auto hitResult = dispatcher.query(events::terrainRaycast::GetTerrainHitQuery{});
            if (hitResult.hit) {
                events::meshBrush::ApplyMeshBrushCommand applyCmd;
                applyCmd.worldPosition = hitResult.position;
                applyCmd.surfaceNormal = hitResult.normal;
                applyCmd.deltaTime = ImGui::GetIO().DeltaTime;
                applyCmd.isFirstApplication = !meshBrushDragging;
                dispatcher.execute(applyCmd);
                meshBrushDragging = true;
            }
        } else {
            if (meshBrushDragging) {
                events::meshBrush::FinalizeMeshBrushCommand finalizeCmd;
                dispatcher.execute(finalizeCmd);
            }
            meshBrushDragging = false;
        }
    }

    void ViewPort::updateFoliageBrushCursorUV(glm::vec2 viewportPos, glm::vec2 viewportSize)
    {
        sendCursorUV(viewportPos, viewportSize);
    }

    void ViewPort::handleFoliageBrush()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (!dispatcher.query(events::foliageBrush::IsFoliageBrushModeActiveQuery{}) || !ImGui::IsWindowHovered() || ImGui::GetIO().KeyCtrl) {
            if (foliageBrushDragging) {
                events::foliageBrush::FinalizeFoliageBrushCommand finalizeCmd;
                dispatcher.execute(finalizeCmd);
            }
            foliageBrushDragging = false;
            return;
        }
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            auto hitResult = dispatcher.query(events::terrainRaycast::GetTerrainHitQuery{});
            if (hitResult.hit) {
                events::foliageBrush::ApplyFoliageBrushCommand applyCmd;
                applyCmd.worldPosition = hitResult.position;
                applyCmd.surfaceNormal = hitResult.normal;
                applyCmd.deltaTime = ImGui::GetIO().DeltaTime;
                applyCmd.isFirstApplication = !foliageBrushDragging;
                dispatcher.execute(applyCmd);
                foliageBrushDragging = true;
            }
        } else {
            if (foliageBrushDragging) {
                events::foliageBrush::FinalizeFoliageBrushCommand finalizeCmd;
                dispatcher.execute(finalizeCmd);
            }
            foliageBrushDragging = false;
        }
    }

    void ViewPort::handleSplineTool()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (!dispatcher.query(events::splineTerrain::IsSplineModeActiveQuery{}) || !ImGui::IsWindowHovered())
        {
            return;
        }

        if (draggedSplinePoint >= 0)
        {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                draggedSplinePoint = -1;
                return;
            }

            auto hitResult = dispatcher.query(events::terrainRaycast::GetTerrainHitQuery{});
            if (hitResult.hit)
            {
                events::splineTerrain::SetSplinePointCommand moveCmd;
                moveCmd.index = static_cast<uint32_t>(draggedSplinePoint);
                moveCmd.position = hitResult.position;
                dispatcher.query(moveCmd);
            }
            return;
        }

        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            return;

        auto hitResult = dispatcher.query(events::terrainRaycast::GetTerrainHitQuery{});
        if (!hitResult.hit)
            return;

        // Grab an existing point if the click landed on one, otherwise append. Picking against the
        // TERRAIN HIT rather than screen space keeps this independent of the camera projection and
        // gives a pick radius the author can reason about in metres.
        const auto points = dispatcher.query(events::splineTerrain::GetActiveSplinePointsQuery{});
        const auto params = dispatcher.query(events::splineTerrain::GetSplineParamsQuery{});
        const float pickRadius = std::max(1.5f, params.corridorWidth * 0.5f);

        int32_t nearest = -1;
        float nearestDistanceSq = pickRadius * pickRadius;
        for (size_t i = 0; i < points.size(); ++i)
        {
            const glm::vec3 delta = points[i].position - hitResult.position;
            const float distanceSq = delta.x * delta.x + delta.z * delta.z;
            if (distanceSq <= nearestDistanceSq)
            {
                nearestDistanceSq = distanceSq;
                nearest = static_cast<int32_t>(i);
            }
        }

        if (nearest >= 0)
        {
            // Alt-click removes a point outright; a plain click starts a drag.
            if (ImGui::GetIO().KeyAlt)
            {
                events::splineTerrain::RemoveSplinePointCommand removeCmd;
                removeCmd.index = static_cast<uint32_t>(nearest);
                dispatcher.query(removeCmd);
            }
            else
            {
                draggedSplinePoint = nearest;
            }
            return;
        }

        events::splineTerrain::AddSplinePointCommand cmd;
        cmd.worldPosition = hitResult.position;
        dispatcher.execute(cmd);
    }

}
