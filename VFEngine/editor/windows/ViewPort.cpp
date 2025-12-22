#include "ViewPort.hpp"
#include "events/EventDispatcher.hpp"
#include "events/RenderEvents.hpp"
#include "events/SceneEvents.hpp"
#include "events/EditorModeEvents.hpp"
#include "time/Timer.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "data/DTOs.hpp"
#include "data/EntityConversion.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <limits>

namespace windows
{
    ViewPort::ViewPort()
        : editorCamera(std::make_unique<editor::EditorCamera>())
    {
    }

    ViewPort::~ViewPort()
    {
        if (iconAtlas.isValid())
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::render::ReleaseEditorTextureCommand cmd;
            cmd.handle = iconAtlas.imguiDescriptorSet;
            dispatcher.execute(cmd);
        }
    }

    void ViewPort::draw()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (ImGui::Begin("ViewPort"))
        {
            // Handle camera input only when viewport is focused AND mouse is hovering
            bool isFocused = ImGui::IsWindowFocused();
            bool isHovered = ImGui::IsWindowHovered();
            if (isFocused && isHovered)
            {
                handleCameraInput();
            }

            ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();

            // Update editor camera aspect ratio based on viewport size
            if (viewportPanelSize.x > 0 && viewportPanelSize.y > 0)
            {
                editorCamera->setAspectRatio(viewportPanelSize.x / viewportPanelSize.y);
            }

            // Update IBL camera matrices with editor camera matrices each frame
            events::render::UpdateIBLCameraCommand cameraCmd;
            cameraCmd.viewMatrix = editorCamera->getViewMatrix();
            cameraCmd.projectionMatrix = editorCamera->getProjectionMatrix();
            dispatcher.execute(cameraCmd);

            events::render::UpdateMeshCameraCommand meshCameraCmd;
            meshCameraCmd.viewMatrix = editorCamera->getViewMatrix();
            meshCameraCmd.projectionMatrix = editorCamera->getProjectionMatrix();
            meshCameraCmd.cameraPosition = editorCamera->position;
            meshCameraCmd.time = static_cast<float>(engineTime::Timer::getElapsedTime());
            dispatcher.execute(meshCameraCmd);

            ImVec2 viewportPos = ImGui::GetCursorScreenPos();

            events::render::GetViewportTextureQuery query;
            auto texture = dispatcher.query(query);
            if (texture.isValid())
            {
                ImGui::Image(texture.imguiDescriptorSet, ImVec2{viewportPanelSize.x, viewportPanelSize.y});
            }

            drawViewportOverlay();
            drawGizmo();

            bool isPlayMode = dispatcher.query(events::editor::IsPlayModeQuery{});

            // Update picking data
            glm::vec2 vp(viewportPos.x, viewportPos.y);
            glm::vec2 vs(viewportPanelSize.x, viewportPanelSize.y);
            if (!isPlayMode)
            {
                updateBillboardScreenPositions(vp, vs);
                updateMeshPickData();
            }

            if (!isPlayMode && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                && !ImGui::IsMouseDown(ImGuiMouseButton_Right)
                && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver())
            {
                ImVec2 mousePos = ImGui::GetMousePos();
                glm::vec2 mp(mousePos.x, mousePos.y);

                // Try billboard picking first
                auto picked = pickBillboardAt(mp);

                // If no billboard hit, try mesh picking
                if (!picked.has_value())
                {
                    picked = pickMeshAt(mp, vp, vs);
                }

                if (picked.has_value())
                {
                    events::scene::SelectEntityCommand cmd;
                    cmd.entity = *picked;
                    dispatcher.execute(cmd);
                }
            }
        }
        ImGui::End();
    }

    void ViewPort::handleCameraInput()
    {
        float dt = static_cast<float>(engineTime::Timer::getDeltaTime());

        // Camera movement (WASD + Q/E)
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

        // Mouse look (right mouse button held)
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

    void ViewPort::updateBillboardScreenPositions(glm::vec2 viewportPos, glm::vec2 viewportSize)
    {
        cachedBillboardHits.clear();

        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
        {
            return;
        }

        glm::mat4 viewMatrix = editorCamera->getViewMatrix();
        glm::mat4 projMatrix = editorCamera->getProjectionMatrix();

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::BillboardComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            const auto& billboard = view.get<components::BillboardComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            // Skip non-selectable or non-editor billboards
            if (!billboard.selectable || !billboard.editorOnly)
            {
                continue;
            }

            glm::vec3 worldPos = glm::vec3(worldTransform.worldMatrix[3]);

            glm::vec4 clipPos = projMatrix * viewMatrix * glm::vec4(worldPos, 1.0f);

            // Behind camera check
            if (clipPos.w <= 0.0f)
            {
                continue;
            }

            // Convert to NDC
            glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;

            if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f)
            {
                continue;
            }

            // Convert to screen space
            // Note: No Y flip needed because EditorCamera's projection matrix already
            // flips Y for Vulkan (projectionMatrix[1][1] *= -1), so ndc.y = -1 is top, +1 is bottom
            glm::vec2 screenPos;
            screenPos.x = (ndc.x * 0.5f + 0.5f) * viewportSize.x + viewportPos.x;
            screenPos.y = (ndc.y * 0.5f + 0.5f) * viewportSize.y + viewportPos.y;

            BillboardScreenHit hit;
            hit.entity = services::internal::toHandle(entity);
            hit.screenCenter = screenPos;
            hit.screenSize = billboard.size; // Size is in screen pixels for ScreenSpace mode

            cachedBillboardHits.push_back(hit);
        }
    }

    std::optional<services::EntityHandle> ViewPort::pickBillboardAt(glm::vec2 screenPos)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        // Iterate in reverse order (last rendered = closest to camera for screen-space billboards)
        for (auto it = cachedBillboardHits.rbegin(); it != cachedBillboardHits.rend(); ++it)
        {
            const auto& hit = *it;

            glm::vec2 halfSize = hit.screenSize * 0.5f;
            glm::vec2 minBounds = hit.screenCenter - halfSize;
            glm::vec2 maxBounds = hit.screenCenter + halfSize;

            if (screenPos.x >= minBounds.x && screenPos.x <= maxBounds.x &&
                screenPos.y >= minBounds.y && screenPos.y <= maxBounds.y)
            {
                // Validate entity still exists and has BillboardComponent (prevents race condition
                // if entity was deleted or component removed between cache update and pick)
                auto enttEntity = services::internal::fromHandle(hit.entity);
                if (registry.valid(enttEntity) &&
                    registry.all_of<components::BillboardComponent>(enttEntity))
                {
                    return hit.entity;
                }
                // Entity was deleted or no longer has billboard, skip and continue searching
            }
        }
        return std::nullopt;
    }

    void ViewPort::updateMeshPickData()
    {
        cachedMeshHits.clear();

        auto& dispatcher = events::EventDispatcher::instance();
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::MeshComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            const auto& meshComp = view.get<components::MeshComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            if (meshComp.meshPath.empty())
            {
                continue;
            }

            // Get mesh bounding box from renderer
            events::render::GetMeshBoundingBoxQuery query;
            query.meshPath = meshComp.meshPath;
            auto bounds = dispatcher.query(query);

            if (!bounds.has_value())
            {
                continue;
            }

            // Transform local AABB to world space
            math::AABB localAABB(bounds->min, bounds->max);
            math::AABB worldAABB = localAABB.getTransformed(worldTransform.worldMatrix);

            MeshPickData pickData;
            pickData.entity = services::internal::toHandle(entity);
            pickData.worldAABB = worldAABB;
            pickData.meshPath = meshComp.meshPath;

            cachedMeshHits.push_back(pickData);
        }
    }

    math::Ray ViewPort::screenToWorldRay(glm::vec2 screenPos, glm::vec2 viewportPos, glm::vec2 viewportSize)
    {
        screenPos.x = glm::clamp(screenPos.x, viewportPos.x, viewportPos.x + viewportSize.x);
        screenPos.y = glm::clamp(screenPos.y, viewportPos.y, viewportPos.y + viewportSize.y);

        // Convert screen position to normalized viewport coordinates [0, 1]
        float normalizedX = (screenPos.x - viewportPos.x) / viewportSize.x;
        float normalizedY = (screenPos.y - viewportPos.y) / viewportSize.y;

        // Convert to NDC [-1, 1]
        // Note: Y is already in correct orientation due to Vulkan's flipped projection
        float ndcX = normalizedX * 2.0f - 1.0f;
        float ndcY = normalizedY * 2.0f - 1.0f;

        // Get inverse matrices
        glm::mat4 invProj = glm::inverse(editorCamera->getProjectionMatrix());
        glm::mat4 invView = glm::inverse(editorCamera->getViewMatrix());

        // Unproject near and far points
        glm::vec4 nearPoint = invProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
        glm::vec4 farPoint = invProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);

        // Perspective divide
        nearPoint /= nearPoint.w;
        farPoint /= farPoint.w;

        // Transform to world space
        glm::vec3 worldNear = glm::vec3(invView * nearPoint);
        glm::vec3 worldFar = glm::vec3(invView * farPoint);

        glm::vec3 direction = glm::normalize(worldFar - worldNear);
        return math::Ray(worldNear, direction);
    }

    std::optional<services::EntityHandle> ViewPort::pickMeshAt(glm::vec2 screenPos, glm::vec2 viewportPos,
                                                               glm::vec2 viewportSize)
    {
        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
        {
            return std::nullopt;
        }

        auto& registry = scene::EntityRegistry::getRegistry();

        math::Ray ray = screenToWorldRay(screenPos, viewportPos, viewportSize);

        // Find closest hit
        float closestDistance = std::numeric_limits<float>::max();
        std::optional<services::EntityHandle> closestEntity;

        for (const auto& meshData : cachedMeshHits)
        {
            auto hitDistance = meshData.worldAABB.intersectRay(ray);
            if (hitDistance.has_value() && *hitDistance < closestDistance)
            {
                // Validate entity still exists
                auto enttEntity = services::internal::fromHandle(meshData.entity);
                if (registry.valid(enttEntity) &&
                    registry.all_of<components::MeshComponent>(enttEntity))
                {
                    closestDistance = *hitDistance;
                    closestEntity = meshData.entity;
                }
            }
        }

        return closestEntity;
    }

    void ViewPort::drawViewportOverlay()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        bool isPlayMode = dispatcher.query(events::editor::IsPlayModeQuery{});

        if (isPlayMode)
        {
            return;
        }

        if (!iconsLoaded)
        {
            loadIconAtlas();
        }

        // Position overlay in top-left of viewport content area
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
        ImVec2 overlayPos = ImVec2(windowPos.x + contentMin.x + 8.0f,
                                   windowPos.y + contentMin.y + 8.0f);

        ImGui::SetNextWindowPos(overlayPos);
        ImGui::SetNextWindowBgAlpha(0.0f);

        ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_AlwaysAutoResize
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoFocusOnAppearing
            | ImGuiWindowFlags_NoNav
            | ImGuiWindowFlags_NoMove;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        if (ImGui::Begin("##ViewportOverlay", nullptr, overlayFlags))
        {
            bool currentGridState = dispatcher.query(events::render::GetShowGridQuery{});

            if (iconAtlas.isValid())
            {
                if (iconButton(ViewportIcon::Grid, currentGridState, "Toggle 3D grid overlay"))
                {
                    events::render::SetShowGridCommand cmd;
                    cmd.show = !currentGridState;
                    dispatcher.execute(cmd);
                }

                ImGui::SameLine();
                bool isWorldMode = (currentGizmoMode == ImGuizmo::WORLD);
                if (iconButton(ViewportIcon::World, isWorldMode, isWorldMode ? "World space" : "Local space"))
                {
                    currentGizmoMode = isWorldMode ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
                }

                ImGui::SameLine();

                if (iconButton(ViewportIcon::Rotate, currentGizmoOp == GizmoOperation::Rotate, "Rotate tool"))
                {
                    currentGizmoOp = (currentGizmoOp == GizmoOperation::Rotate)
                                         ? GizmoOperation::None
                                         : GizmoOperation::Rotate;
                }

                ImGui::SameLine();

                if (iconButton(ViewportIcon::Scale, currentGizmoOp == GizmoOperation::Scale, "Scale tool"))
                {
                    currentGizmoOp = (currentGizmoOp == GizmoOperation::Scale)
                                         ? GizmoOperation::None
                                         : GizmoOperation::Scale;
                }

                ImGui::SameLine();

                if (iconButton(ViewportIcon::Translate, currentGizmoOp == GizmoOperation::Translate, "Move tool"))
                {
                    currentGizmoOp = (currentGizmoOp == GizmoOperation::Translate)
                                         ? GizmoOperation::None
                                         : GizmoOperation::Translate;
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void ViewPort::loadIconAtlas()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::render::LoadEditorTextureCommand cmd;
        cmd.path = "../../resources/editor/viewPortAtlasIcons.vfImage";
        cmd.isHDR = false;
        iconAtlas = dispatcher.execute(cmd);

        iconsLoaded = true;
    }

    std::pair<glm::vec2, glm::vec2> ViewPort::getIconUV(ViewportIcon icon) const
    {
        uint32_t index = static_cast<uint32_t>(icon);
        uint32_t maxIndex = ATLAS_COLUMNS * ATLAS_ROWS;

        // Bounds check - fallback to first icon if out of range
        if (index >= maxIndex)
        {
            index = 0;
        }

        float colSize = 1.0f / static_cast<float>(ATLAS_COLUMNS);
        float rowSize = 1.0f / static_cast<float>(ATLAS_ROWS);

        float col = static_cast<float>(index % ATLAS_COLUMNS);
        float row = static_cast<float>(index / ATLAS_COLUMNS);

        glm::vec2 uv0(col * colSize, row * rowSize);
        glm::vec2 uv1((col + 1.0f) * colSize, (row + 1.0f) * rowSize);

        return {uv0, uv1};
    }

    bool ViewPort::iconButton(ViewportIcon icon, bool isActive, const char* tooltip)
    {
        auto [uv0, uv1] = getIconUV(icon);

        ImGui::PushID(static_cast<int>(icon));

        ImVec4 bgColor = isActive ? ImVec4(0.3f, 0.5f, 0.8f, 1.0f) : ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
        ImVec4 tintColor = isActive ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, bgColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(bgColor.x + 0.1f, bgColor.y + 0.1f, bgColor.z + 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              ImVec4(bgColor.x + 0.2f, bgColor.y + 0.2f, bgColor.z + 0.2f, 1.0f));

        bool clicked = ImGui::ImageButton(
            "##iconBtn",
            iconAtlas.imguiDescriptorSet,
            ImVec2(ICON_SIZE, ICON_SIZE),
            ImVec2(uv0.x, uv0.y),
            ImVec2(uv1.x, uv1.y),
            ImVec4(0.0f, 0.0f, 0.0f, 0.0f), // bg_col (transparent)
            tintColor
        );

        ImGui::PopStyleColor(3);

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", tooltip);
        }

        ImGui::PopID();

        return clicked;
    }

    void ViewPort::drawGizmo()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Skip in play mode or if no operation selected
        if (dispatcher.query(events::editor::IsPlayModeQuery{})) return;
        if (currentGizmoOp == GizmoOperation::None) return;

        // Get selected entity
        auto selectedEntity = dispatcher.query(events::scene::GetSelectedEntityQuery{});
        if (!selectedEntity.has_value()) return;

        // Get transform
        events::scene::GetTransformQuery transformQuery;
        transformQuery.entity = *selectedEntity;
        auto transformOpt = dispatcher.query(transformQuery);
        if (!transformOpt.has_value()) return;

        // Setup viewport rect
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
        ImVec2 contentMax = ImGui::GetWindowContentRegionMax();
        float vpX = windowPos.x + contentMin.x;
        float vpY = windowPos.y + contentMin.y;
        float vpW = contentMax.x - contentMin.x;
        float vpH = contentMax.y - contentMin.y;

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(vpX, vpY, vpW, vpH);

        // Get matrices - undo Vulkan Y-flip for ImGuizmo (it expects OpenGL-style projection)
        glm::mat4 view = editorCamera->getViewMatrix();
        glm::mat4 proj = editorCamera->getProjectionMatrix();
        proj[1][1] *= -1.0f;  // Undo Vulkan Y-flip for ImGuizmo
        glm::mat4 objectMatrix = buildTransformMatrix(*transformOpt);

        // Map operation
        ImGuizmo::OPERATION op;
        switch (currentGizmoOp)
        {
        case GizmoOperation::Translate: op = ImGuizmo::TRANSLATE;
            break;
        case GizmoOperation::Rotate: op = ImGuizmo::ROTATE;
            break;
        case GizmoOperation::Scale: op = ImGuizmo::SCALE;
            break;
        default: return;
        }

        // Manipulate and update if changed
        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                                 op, currentGizmoMode, glm::value_ptr(objectMatrix)))
        {
            events::scene::SetTransformCommand cmd;
            cmd.entity = *selectedEntity;
            cmd.transform = decomposeTransformMatrix(objectMatrix);
            dispatcher.execute(cmd);
        }
    }

    glm::mat4 ViewPort::buildTransformMatrix(const services::TransformData& transform) const
    {
        float translation[3] = {transform.position.x, transform.position.y, transform.position.z};
        float rotation[3] = {transform.rotation.x, transform.rotation.y, transform.rotation.z};
        float scale[3] = {transform.scale.x, transform.scale.y, transform.scale.z};

        glm::mat4 mat(1.0f);
        ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, glm::value_ptr(mat));
        return mat;
    }

    services::TransformData ViewPort::decomposeTransformMatrix(const glm::mat4& matrix) const
    {
        float translation[3], rotation[3], scale[3];
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(matrix), translation, rotation, scale);

        services::TransformData result;
        result.position = glm::vec3(translation[0], translation[1], translation[2]);
        result.rotation = glm::vec3(rotation[0], rotation[1], rotation[2]);
        result.scale = glm::vec3(scale[0], scale[1], scale[2]);

        return result;
    }
}
