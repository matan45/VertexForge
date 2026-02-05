#include "ViewPort.hpp"
#include "events/EventDispatcher.hpp"
#include "events/RenderEvents.hpp"
#include "events/SceneEvents.hpp"
#include "events/EditorModeEvents.hpp"
#include "events/SculptModeEvents.hpp"
#include "events/TerrainRaycastEvents.hpp"
#include "events/BrushEvents.hpp"
#include "events/AudioEvents.hpp"
#include "time/Timer.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "data/DTOs.hpp"
#include "data/EntityConversion.hpp"
#include "../dragdrop/DragDropManager.hpp"
#include <imgui.h>
#include "ImGuizmo.h"
#include <glm/gtc/type_ptr.hpp>
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

            // Update sculpt cursor UV BEFORE render so raycast uses current mouse position
            updateSculptCursorUV(vp, vs);

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
            drawSculptDebugOverlay(vp, vs);
        }
        ImGui::End();
    }

    CameraState ViewPort::getActiveCameraState(bool isPlayMode, float aspectRatio)
    {
        CameraState state;
        auto& dispatcher = events::EventDispatcher::instance();

        bool usingGameCamera = false;
        if (isPlayMode)
        {
            auto primaryCameraOpt = dispatcher.query(events::scene::GetPrimaryCameraQuery{});
            if (primaryCameraOpt.has_value())
            {
                auto primaryCamera = *primaryCameraOpt;

                events::scene::GetCameraDataQuery cameraQuery;
                cameraQuery.entity = primaryCamera;
                auto cameraDataOpt = dispatcher.query(cameraQuery);

                if (cameraDataOpt.has_value())
                {
                    events::scene::GetTransformQuery transformQuery;
                    transformQuery.entity = primaryCamera;
                    auto transformOpt = dispatcher.query(transformQuery);

                    if (transformOpt.has_value())
                    {
                        auto& transform = *transformOpt;

                        auto enttEntity = services::internal::fromHandle(primaryCamera);
                        auto& registry = scene::EntityRegistry::getRegistry();
                        if (registry.all_of<components::CameraComponent>(enttEntity))
                        {
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

                            usingGameCamera = true;
                        }
                    }
                }
            }
        }

        if (!usingGameCamera)
        {
            state.viewMatrix = editorCamera->getViewMatrix();
            state.projectionMatrix = editorCamera->getProjectionMatrix();
            state.position = editorCamera->position;
            state.forward = editorCamera->getForwardDirection();
        }

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

        // Accept drops from content browser
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DND_CONTENT_BROWSER))
        {
            const auto& dragPaths = DragDropManager::instance().getDragPaths();

            for (const auto& path : dragPaths)
            {
                std::filesystem::path fsPath(path);

                // Only handle prefab files
                if (fsPath.extension() != ".vfPrefab")
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

    void ViewPort::updateSculptCursorUV(glm::vec2 viewportPos, glm::vec2 viewportSize)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        bool sculptActive = dispatcher.query(events::sculpt::IsSculptModeActiveQuery{});

        if (!sculptActive || !ImGui::IsWindowHovered())
        {
            dispatcher.execute(events::terrainRaycast::ClearCursorCommand{});
            return;
        }

        ImVec2 mousePos = ImGui::GetMousePos();
        glm::vec2 uv = (glm::vec2(mousePos.x, mousePos.y) - viewportPos) / viewportSize;
        uv = glm::clamp(uv, glm::vec2(0.0f), glm::vec2(1.0f));

        events::terrainRaycast::SetCursorPositionCommand cmd;
        cmd.cursorUV = uv;
        dispatcher.execute(cmd);
    }

    void ViewPort::handleSculptBrush()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        bool sculptActive = dispatcher.query(events::sculpt::IsSculptModeActiveQuery{});

        if (!sculptActive || !ImGui::IsWindowHovered())
        {
            sculptDragging = false;
            return;
        }

        // Apply brush on left-click/drag
        bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        bool shiftHeld = ImGui::GetIO().KeyShift;

        if (leftDown)
        {
            auto hitResult = dispatcher.query(events::terrainRaycast::GetTerrainHitQuery{});
            if (hitResult.hit)
            {
                events::brush::ApplyBrushCommand applyCmd;
                applyCmd.worldPosition = hitResult.position;
                applyCmd.deltaTime = ImGui::GetIO().DeltaTime;
                applyCmd.invert = shiftHeld;
                applyCmd.isFirstApplication = !sculptDragging;
                dispatcher.execute(applyCmd);

                sculptDragging = true;
            }
        }
        else
        {
            sculptDragging = false;
        }
    }

    void ViewPort::drawSculptDebugOverlay(glm::vec2 viewportPos, glm::vec2 viewportSize)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        bool sculptActive = dispatcher.query(events::sculpt::IsSculptModeActiveQuery{});
        if (!sculptActive)
        {
            return;
        }

        // Draw checkbox at top-right of viewport
        ImGui::SetCursorScreenPos(ImVec2(viewportPos.x + viewportSize.x - 150.0f, viewportPos.y + 5.0f));
        ImGui::Checkbox("Sculpt Debug", &showSculptDebug);

        if (!showSculptDebug)
        {
            return;
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 mousePos = ImGui::GetMousePos();

        // Red crosshair: raw mouse position
        float crossSize = 10.0f;
        ImU32 redColor = IM_COL32(255, 0, 0, 255);
        drawList->AddLine(
            ImVec2(mousePos.x - crossSize, mousePos.y),
            ImVec2(mousePos.x + crossSize, mousePos.y), redColor, 2.0f);
        drawList->AddLine(
            ImVec2(mousePos.x, mousePos.y - crossSize),
            ImVec2(mousePos.x, mousePos.y + crossSize), redColor, 2.0f);

        // Green circle: projected terrain hit position
        auto hitResult = dispatcher.query(events::terrainRaycast::GetTerrainHitQuery{});
        if (hitResult.hit)
        {
            glm::mat4 viewProj = editorCamera->getProjectionMatrix() * editorCamera->getViewMatrix();
            glm::vec4 clipPos = viewProj * glm::vec4(hitResult.position, 1.0f);

            if (clipPos.w > 0.0f)
            {
                glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;

                // NDC to viewport screen position
                float screenX = viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
                float screenY = viewportPos.y + (ndc.y * 0.5f + 0.5f) * viewportSize.y;

                ImU32 greenColor = IM_COL32(0, 255, 0, 255);
                drawList->AddCircle(ImVec2(screenX, screenY), 8.0f, greenColor, 0, 2.0f);
                drawList->AddLine(
                    ImVec2(screenX - crossSize, screenY),
                    ImVec2(screenX + crossSize, screenY), greenColor, 2.0f);
                drawList->AddLine(
                    ImVec2(screenX, screenY - crossSize),
                    ImVec2(screenX, screenY + crossSize), greenColor, 2.0f);

                // Text info with background - anchored to bottom-left of viewport
                ImU32 bgColor = IM_COL32(0, 0, 0, 180);
                ImU32 textColor = IM_COL32(255, 255, 255, 255);
                float textX = viewportPos.x + 10.0f;
                float bottomY = viewportPos.y + viewportSize.y;

                char buf[128];

                snprintf(buf, sizeof(buf), "Red = Mouse | Green = Raycast Hit");
                ImVec2 textSize = ImGui::CalcTextSize(buf);
                float textY = bottomY - 18.0f * 3 - 6.0f;
                drawList->AddRectFilled(
                    ImVec2(textX - 2, textY - 2),
                    ImVec2(textX + 4 + textSize.x, textY + textSize.y + 2), bgColor, 3.0f);
                drawList->AddText(ImVec2(textX, textY),
                                  IM_COL32(200, 200, 200, 255), buf);
                textY += 18.0f;

                snprintf(buf, sizeof(buf), "Hit: (%.1f, %.1f, %.1f)",
                         hitResult.position.x, hitResult.position.y, hitResult.position.z);
                textSize = ImGui::CalcTextSize(buf);
                drawList->AddRectFilled(
                    ImVec2(textX - 2, textY - 2),
                    ImVec2(textX + 4 + textSize.x, textY + textSize.y + 2), bgColor, 3.0f);
                drawList->AddText(ImVec2(textX, textY), textColor, buf);
                textY += 18.0f;

                float dx = screenX - mousePos.x;
                float dy = screenY - mousePos.y;
                float offset = std::sqrt(dx * dx + dy * dy);
                snprintf(buf, sizeof(buf), "Offset: %.1f px", offset);
                textSize = ImGui::CalcTextSize(buf);
                drawList->AddRectFilled(
                    ImVec2(textX - 2, textY - 2),
                    ImVec2(textX + 4 + textSize.x, textY + textSize.y + 2), bgColor, 3.0f);
                drawList->AddText(ImVec2(textX, textY), textColor, buf);
            }
        }
        else
        {
            float textY = viewportPos.y + viewportSize.y - 24.0f;
            ImU32 bgColor = IM_COL32(0, 0, 0, 180);
            const char* msg = "No terrain hit";
            ImVec2 textSize = ImGui::CalcTextSize(msg);
            drawList->AddRectFilled(
                ImVec2(viewportPos.x + 8, textY - 2),
                ImVec2(viewportPos.x + 14 + textSize.x, textY + textSize.y + 2), bgColor, 3.0f);
            drawList->AddText(ImVec2(viewportPos.x + 10, textY),
                              IM_COL32(255, 100, 100, 255), msg);
        }
    }
}
