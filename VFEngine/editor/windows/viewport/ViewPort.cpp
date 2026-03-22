#include "ViewPort.hpp"
#include "events/EventDispatcher.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/editor/SculptModeEvents.hpp"
#include "events/terrain/TerrainRaycastEvents.hpp"
#include "events/terrain/BrushEvents.hpp"
#include "events/terrain/PaintModeEvents.hpp"
#include "events/terrain/PaintBrushEvents.hpp"
#include "events/terrain/HoleModeEvents.hpp"
#include "events/terrain/HoleBrushEvents.hpp"
#include "events/terrain/CaveModeEvents.hpp"
#include "events/terrain/CaveBrushEvents.hpp"
#include "events/vegetation/VegetationBrushEvents.hpp"
#include "events/meshbrush/MeshBrushEvents.hpp"
#include "events/audio/AudioEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "time/Timer.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "data/DTOs.hpp"
#include "data/EntityConversion.hpp"
#include "../../dragdrop/DragDropManager.hpp"
#include <imgui.h>
#include "ImGuizmo.h"
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

            updateBrushCursors(vp, vs);

            events::render::GetViewportTextureQuery query;
            auto texture = dispatcher.query(query);
            if (texture.isValid())
            {
                ImGui::Image(texture.imguiDescriptorSet, ImVec2{viewportPanelSize.x, viewportPanelSize.y});
                handlePrefabDrop();
            }

            overlay.draw(gizmo);
            gizmo.draw(*editorCamera);

            if (!isPlayMode)
            {
                picker.updateBillboardScreenPositions(*editorCamera, vp, vs);
                picker.updateMeshPickData();
            }

            handleEntityPicking(isPlayMode, vp, vs);
            handleSculptBrush();
            handlePaintBrush();
            handleHoleBrush();
            handleCaveBrush();
            handleVegetationBrush();
            handleMeshBrush();
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

        auto& camComp = registry.get<components::CameraComponent>(enttEntity);
        camComp.aspectRatio = aspectRatio;
        camComp.updateProjectionMatrix();
        camComp.updateViewMatrix(transform.position, transform.rotation);

        state.viewMatrix = camComp.viewMatrix;
        state.projectionMatrix = camComp.projectionMatrix;
        state.position = transform.position;

        glm::mat4 rotMat = glm::mat4(1.0f);
        rotMat = glm::rotate(rotMat, glm::radians(transform.rotation.y), glm::vec3(0, 1, 0));
        rotMat = glm::rotate(rotMat, glm::radians(transform.rotation.x), glm::vec3(1, 0, 0));
        state.forward = glm::normalize(glm::vec3(rotMat * glm::vec4(0, 0, -1, 0)));
        return true;
    }

    CameraState ViewPort::getActiveCameraState(bool isPlayMode, float aspectRatio)
    {
        CameraState state;
        if (isPlayMode && tryGetGameCameraState(state, aspectRatio))
            return state;

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

    void ViewPort::handlePrefabDrop()
    {
        if (!ImGui::BeginDragDropTarget())
        {
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DND_CONTENT_BROWSER))
        {
            const auto& dragPaths = DragDropManager::instance().getDragPaths();

            for (const auto& path : dragPaths)
            {
                std::filesystem::path fsPath(path);
                auto ext = fsPath.extension().string();

                if (ext == ".vfTerrain")
                {
                    events::terrain::BeginTerrainLoadCommand loadCmd;
                    loadCmd.path = path;
                    dispatcher.execute(loadCmd);
                    continue;
                }

                if (ext != ".vfPrefab")
                {
                    continue;
                }

                events::scene::LoadPrefabCommand loadCmd;
                loadCmd.filePath = path;
                loadCmd.parent = std::nullopt;
                auto result = dispatcher.execute(loadCmd);

                if (result.has_value())
                {
                    events::scene::GetTransformQuery transformQuery;
                    transformQuery.entity = *result;
                    auto prefabTransform = dispatcher.query(transformQuery);

                    if (prefabTransform.has_value())
                    {
                        glm::vec3 spawnPos = editorCamera->position + editorCamera->getForwardDirection() * 5.0f;

                        services::TransformData transform;
                        transform.position = spawnPos;
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
            }

            DragDropManager::instance().endDrag();
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

        bool anyActive = sculptActive || paintActive || holeActive || caveActive || vegActive || meshBrushActive;

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
        if (!dispatcher.query(events::cave::IsCaveModeActiveQuery{}) || !ImGui::IsWindowHovered()) {
            caveDragging = false;
            return;
        }
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            auto hitResult = dispatcher.query(events::terrainRaycast::GetTerrainHitQuery{});
            if (hitResult.hit) {
                events::caveBrush::ApplyCaveBrushCommand applyCmd;
                applyCmd.worldPosition = hitResult.position;
                applyCmd.deltaTime = ImGui::GetIO().DeltaTime;
                applyCmd.invert = ImGui::GetIO().KeyShift;
                applyCmd.isFirstApplication = !caveDragging;
                dispatcher.execute(applyCmd);
                caveDragging = true;
            }
        } else {
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
            vegetationDragging = false;
            return;
        }
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            auto hitResult = dispatcher.query(events::terrainRaycast::GetTerrainHitQuery{});
            if (hitResult.hit) {
                events::vegetationBrush::ApplyVegetationDensityBrushCommand applyCmd;
                applyCmd.worldPosition = hitResult.position;
                applyCmd.deltaTime = ImGui::GetIO().DeltaTime;
                applyCmd.invert = ImGui::GetIO().KeyShift;
                applyCmd.isFirstApplication = !vegetationDragging;
                dispatcher.execute(applyCmd);
                vegetationDragging = true;
            }
        } else {
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

}
