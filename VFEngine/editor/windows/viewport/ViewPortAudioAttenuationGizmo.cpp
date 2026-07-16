#include "ViewPortAudioAttenuationGizmo.hpp"
#include "AudioSource3DDistanceUndo.hpp"
#include "ViewPortPicker.hpp"
#include "../../camera/EditorCamera.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/SculptModeEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/meshbrush/MeshBrushEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/terrain/CaveModeEvents.hpp"
#include "events/terrain/HoleModeEvents.hpp"
#include "events/terrain/PaintModeEvents.hpp"
#include "events/terrain/SplineTerrainEvents.hpp"
#include "events/vegetation/VegetationBrushEvents.hpp"
#include <imgui.h>
#include "ImGuizmo.h"
#include <memory>

namespace
{
    constexpr float handleRadius = 6.0f;
    constexpr float hoveredHandleRadius = 8.0f;
    constexpr float handleHitRadius = 10.0f;
    constexpr ImU32 minHandleColor = IM_COL32(0, 255, 0, 255);
    constexpr ImU32 maxHandleColor = IM_COL32(255, 153, 0, 255);
    constexpr ImU32 handleOutlineColor = IM_COL32(255, 255, 255, 230);

    bool sameEntity(const services::EntityHandle& lhs, const services::EntityHandle& rhs)
    {
        return lhs.id == rhs.id;
    }
}

namespace windows
{
    bool ViewPortAudioAttenuationGizmo::isToolModeActive() const
    {
        auto& dispatcher = events::EventDispatcher::instance();
        return dispatcher.query(events::sculpt::IsSculptModeActiveQuery{}) ||
               dispatcher.query(events::paint::IsPaintModeActiveQuery{}) ||
               dispatcher.query(events::hole::IsHoleModeActiveQuery{}) ||
               dispatcher.query(events::cave::IsCaveModeActiveQuery{}) ||
               dispatcher.query(events::vegetationBrush::IsVegetationBrushModeActiveQuery{}) ||
               dispatcher.query(events::meshBrush::IsMeshBrushModeActiveQuery{}) ||
               dispatcher.query(events::splineTerrain::IsSplineModeActiveQuery{});
    }

    bool ViewPortAudioAttenuationGizmo::isContextStillEditable(bool viewportAvailable,
                                                               bool isPlayMode,
                                                               bool windowFocused,
                                                               bool cameraLookActive) const
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (!viewportAvailable || isPlayMode || !windowFocused || cameraLookActive ||
            isToolModeActive() || !dispatcher.query(events::render::GetShowDebugRenderingQuery{}))
        {
            return false;
        }

        const auto selected = dispatcher.query(events::scene::GetSelectedEntitiesQuery{});
        if (selected.empty() || !sameEntity(selected.front(), dragEntity))
        {
            return false;
        }

        events::scene::GetAudioSource3DDataQuery audioQuery;
        audioQuery.entity = dragEntity;
        const auto audio = dispatcher.query(audioQuery);
        return audio.has_value() && audio->showDebugSpheres;
    }

    void ViewPortAudioAttenuationGizmo::draw(const editor::EditorCamera& camera,
                                             ViewPortPicker& picker,
                                             glm::vec2 viewportPosition,
                                             glm::vec2 viewportSize,
                                             bool viewportAvailable,
                                             bool isPlayMode,
                                             bool windowHovered,
                                             bool windowFocused,
                                             bool cameraLookActive)
    {
        hoveredHandle = audioattenuation::HandleKind::None;

        // A captured drag closes before any selection/mode visibility early-out.
        if (dragging)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                cancelDrag();
                return;
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                !isContextStillEditable(viewportAvailable, isPlayMode, windowFocused, cameraLookActive))
            {
                finalizeDrag();
                return;
            }
            if (!updateDrag(camera, picker, viewportPosition, viewportSize))
            {
                abandonDrag();
                return;
            }
            hoveredHandle = draggedHandle;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }

        auto& dispatcher = events::EventDispatcher::instance();
        if (!viewportAvailable || isPlayMode || cameraLookActive || isToolModeActive() ||
            !dispatcher.query(events::render::GetShowDebugRenderingQuery{}))
        {
            return;
        }

        const auto selected = dispatcher.query(events::scene::GetSelectedEntitiesQuery{});
        if (selected.empty())
        {
            return;
        }
        const services::EntityHandle entity = selected.front();

        events::scene::GetAudioSource3DDataQuery audioQuery;
        audioQuery.entity = entity;
        const auto audio = dispatcher.query(audioQuery);
        if (!audio.has_value() || !audio->showDebugSpheres)
        {
            return;
        }

        events::scene::GetWorldTransformQuery worldQuery;
        worldQuery.entity = entity;
        const auto world = dispatcher.query(worldQuery);
        if (!world.has_value())
        {
            return;
        }

        const audioattenuation::Distances distances{audio->minDistance, audio->maxDistance};
        const auto layout = audioattenuation::buildHandleLayout(
            world->position, distances, camera.getViewMatrix(), camera.getProjectionMatrix(),
            viewportPosition, viewportSize);
        if (!layout.has_value())
        {
            return;
        }

        const ImVec2 mouse = ImGui::GetMousePos();
        const glm::vec2 mousePosition(mouse.x, mouse.y);
        if (!dragging)
        {
            hoveredHandle = audioattenuation::hitTest(*layout, mousePosition, handleHitRadius);
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        auto drawHandle = [&](audioattenuation::HandleKind kind, const glm::vec2& position, ImU32 color)
        {
            const float radius = hoveredHandle == kind ? hoveredHandleRadius : handleRadius;
            const ImVec2 screenPosition(position.x, position.y);
            drawList->AddCircleFilled(screenPosition, radius, color, 16);
            drawList->AddCircle(screenPosition, radius, handleOutlineColor, 16, 1.5f);
        };
        if (layout->minVisible)
        {
            drawHandle(audioattenuation::HandleKind::MinDistance, layout->minHandle, minHandleColor);
        }
        if (layout->maxVisible)
        {
            drawHandle(audioattenuation::HandleKind::MaxDistance, layout->maxHandle, maxHandleColor);
        }

        if (hoveredHandle != audioattenuation::HandleKind::None)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }

        if (!dragging && windowHovered && windowFocused &&
            hoveredHandle != audioattenuation::HandleKind::None &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGuizmo::IsUsing() && !ImGuizmo::IsOver())
        {
            dragging = true;
            draggedHandle = hoveredHandle;
            dragEntity = entity;
            beforeDistances = distances;
            dragCenter = world->position;
            dragPlaneNormal = layout->planeNormal;
            dragOutwardAxis = draggedHandle == audioattenuation::HandleKind::MinDistance
                ? layout->cameraRight
                : -layout->cameraRight;
        }
    }

    bool ViewPortAudioAttenuationGizmo::updateDrag(const editor::EditorCamera& camera,
                                                   ViewPortPicker& picker,
                                                   glm::vec2 viewportPosition,
                                                   glm::vec2 viewportSize)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        events::scene::GetAudioSource3DDataQuery audioQuery;
        audioQuery.entity = dragEntity;
        const auto audio = dispatcher.query(audioQuery);
        if (!audio.has_value())
        {
            return false;
        }

        const ImVec2 mouse = ImGui::GetMousePos();
        const math::Ray ray = picker.screenToWorldRay(
            camera, glm::vec2(mouse.x, mouse.y), viewportPosition, viewportSize);
        const auto candidate = audioattenuation::radiusAlongAxis(
            ray, dragCenter, dragPlaneNormal, dragOutwardAxis);
        if (!candidate.has_value())
        {
            return true;
        }

        const auto constrained = audioattenuation::constrainDraggedDistance(
            draggedHandle, *candidate,
            audioattenuation::Distances{audio->minDistance, audio->maxDistance});
        if (!constrained.has_value())
        {
            return true;
        }

        events::scene::SetAudioSource3DDistancesCommand command;
        command.entity = dragEntity;
        command.minDistance = constrained->minDistance;
        command.maxDistance = constrained->maxDistance;
        return dispatcher.execute(command);
    }

    void ViewPortAudioAttenuationGizmo::finalizeDrag()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        events::scene::GetAudioSource3DDataQuery audioQuery;
        audioQuery.entity = dragEntity;
        const auto audio = dispatcher.query(audioQuery);
        if (audio.has_value())
        {
            const audioattenuation::Distances after{audio->minDistance, audio->maxDistance};
            if (!(beforeDistances == after))
            {
                events::undoredo::PushUndoableCommand push;
                push.command = std::make_shared<AudioSource3DDistanceUndoCommand>(
                    dragEntity, beforeDistances.minDistance, beforeDistances.maxDistance,
                    after.minDistance, after.maxDistance);
                dispatcher.execute(push);
            }
        }
        abandonDrag();
    }

    void ViewPortAudioAttenuationGizmo::cancelDrag()
    {
        events::scene::SetAudioSource3DDistancesCommand command;
        command.entity = dragEntity;
        command.minDistance = beforeDistances.minDistance;
        command.maxDistance = beforeDistances.maxDistance;
        events::EventDispatcher::instance().execute(command);
        abandonDrag();
    }

    void ViewPortAudioAttenuationGizmo::abandonDrag()
    {
        dragging = false;
        hoveredHandle = audioattenuation::HandleKind::None;
        draggedHandle = audioattenuation::HandleKind::None;
    }
}
