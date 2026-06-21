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
#include "events/terrain/CaveModeEvents.hpp"
#include "events/terrain/CaveBrushEvents.hpp"
#include "events/vegetation/VegetationBrushEvents.hpp"
#include "events/meshbrush/MeshBrushEvents.hpp"
#include "events/ui/UIPickEvents.hpp"
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

            if (isFocused && isHovered && !isPlayMode)
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

            overlay.draw(gizmo);
            gizmo.draw(*editorCamera);

            if (!isPlayMode)
            {
                picker.updateBillboardScreenPositions(*editorCamera, vp, vs);
                picker.updateMeshPickData();
                drawSelectedUIOutline(vp, vs);
            }

            handleEntityPicking(isPlayMode, vp, vs);
            handleSculptBrush();
            handlePaintBrush();
            handleHoleBrush();
            handleCaveBrush();
            handleVegetationBrush();
            handleMeshBrush();
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

    void ViewPort::handleEntityPicking(bool isPlayMode, glm::vec2 viewportPos, glm::vec2 viewportSize)
    {
        if (isPlayMode) return;

        auto& sculptDispatcher = events::EventDispatcher::instance();
        if (sculptDispatcher.query(events::sculpt::IsSculptModeActiveQuery{})) return;
        if (sculptDispatcher.query(events::paint::IsPaintModeActiveQuery{})) return;
        if (sculptDispatcher.query(events::hole::IsHoleModeActiveQuery{})) return;
        if (sculptDispatcher.query(events::vegetationBrush::IsVegetationBrushModeActiveQuery{})) return;
        bool meshBrushActive = sculptDispatcher.query(events::meshBrush::IsMeshBrushModeActiveQuery{});
        if (meshBrushActive && !ImGui::GetIO().KeyCtrl) return;

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
            auto& dispatcher = events::EventDispatcher::instance();
            events::scene::SelectEntityCommand cmd;
            cmd.entity = *picked;
            dispatcher.execute(cmd);
        }
    }

    void ViewPort::drawSelectedUIOutline(glm::vec2 viewportPos, glm::vec2 viewportSize)
    {
        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) return;

        auto& dispatcher = events::EventDispatcher::instance();
        auto selected = dispatcher.query(events::scene::GetSelectedEntityQuery{});
        if (!selected.has_value()) return;

        events::ui::GetUIEntityWorldQuadQuery quadQuery;
        quadQuery.entity = *selected;
        auto quad = dispatcher.query(quadQuery);
        if (!quad.has_value()) return;

        glm::mat4 viewProj = editorCamera->getProjectionMatrix() * editorCamera->getViewMatrix();

        glm::vec4 clipCorners[4];
        for (int i = 0; i < 4; ++i)
        {
            clipCorners[i] = viewProj * glm::vec4(quad->corners[i], 1.0f);
        }

        // No Y flip — EditorCamera's projection already flips Y for Vulkan
        auto project = [&](const glm::vec4& clip) {
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            return ImVec2((ndc.x * 0.5f + 0.5f) * viewportSize.x + viewportPos.x,
                          (ndc.y * 0.5f + 0.5f) * viewportSize.y + viewportPos.y);
        };

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
        if (pointCount < 2) return; // fully behind the camera

        ImGui::GetWindowDrawList()->AddPolyline(points, pointCount, IM_COL32(255, 161, 0, 255),
                                                ImDrawFlags_Closed, 2.0f);
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

        if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            ImVec2 mousePos = ImGui::GetMousePos();

            if (isFirstMouseInput)
            {
                lastMouseX = mousePos.x;
                lastMouseY = mousePos.y;
                isFirstMouseInput = false;
            }
            else
            {
                float xOffset = mousePos.x - lastMouseX;
                float yOffset = mousePos.y - lastMouseY;

                editorCamera->processMouseMovement(xOffset, yOffset);

                lastMouseX = mousePos.x;
                lastMouseY = mousePos.y;
            }
        }
        else
        {
            isFirstMouseInput = true;
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

        bool splineActive = dispatcher.query(events::splineTerrain::IsSplineModeActiveQuery{});

        bool anyActive = sculptActive || paintActive || holeActive || caveActive || vegActive || meshBrushActive || splineActive;

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
        if (!dispatcher.query(events::hole::IsHoleModeActiveQuery{}) || !ImGui::IsWindowHovered()) return;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            auto hitResult = dispatcher.query(events::terrainRaycast::GetTerrainHitQuery{});
            if (hitResult.hit) {
                events::holeBrush::ApplyHoleBrushCommand applyCmd;
                applyCmd.worldPosition = hitResult.position;
                applyCmd.erase = ImGui::GetIO().KeyShift;
                dispatcher.execute(applyCmd);
            }
        }
    }

    void ViewPort::handleCaveBrush()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (!dispatcher.query(events::cave::IsCaveModeActiveQuery{}) || !ImGui::IsWindowHovered())
        {
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
            meshBrushDragging = false;
        }
    }

    void ViewPort::handleSplineTool()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (!dispatcher.query(events::splineTerrain::IsSplineModeActiveQuery{}) || !ImGui::IsWindowHovered())
        {
            return;
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            auto hitResult = dispatcher.query(events::terrainRaycast::GetTerrainHitQuery{});
            if (hitResult.hit)
            {
                events::splineTerrain::AddSplinePointCommand cmd;
                cmd.worldPosition = hitResult.position;
                dispatcher.execute(cmd);
            }
        }
    }

}
