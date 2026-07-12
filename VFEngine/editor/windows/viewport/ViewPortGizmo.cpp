#include "ViewPortGizmo.hpp"
#include "GroupTransformMath.hpp"
#include "SceneEntityTransformUndo.hpp"
#include "../../camera/EditorCamera.hpp"
#include "../../selection/SelectionPolicy.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/editor/SculptModeEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/project/SceneEvents.hpp"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <optional>

namespace windows
{
    void ViewPortGizmo::draw(const editor::EditorCamera& camera)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Sampled before Manipulate: reflects the drag state as of last frame.
        const bool usingGizmo = ImGuizmo::IsUsing();

        // Falling edge FIRST, before any early-out that depends on the current
        // selection or mode — a finished drag must always close into its undo
        // record even if the selection changed or play mode started mid-drag.
        if (!usingGizmo && dragActive)
        {
            finalizeDrag();
        }

        // Skip in play mode, sculpt mode, or if no operation selected
        if (dispatcher.query(events::editor::IsPlayModeQuery{})) return;
        if (dispatcher.query(events::sculpt::IsSculptModeActiveQuery{})) return;
        if (currentGizmoOp == GizmoOperation::None) return;

        auto selectedEntities = dispatcher.query(events::scene::GetSelectedEntitiesQuery{});
        if (selectedEntities.empty()) return;
        const services::EntityHandle activeEntity = selectedEntities.front();
        const bool group = selectedEntities.size() > 1;

        // Pre-Manipulate reads — pristine even on the drag's first frame.
        // Single selection keeps the existing local-transform path; a group
        // manipulates the ACTIVE entity's WORLD matrix so the delta is world-
        // space regardless of the active entity's parenting.
        services::TransformData pristine;
        glm::mat4 objectMatrix(1.0f);
        if (group)
        {
            events::scene::GetWorldTransformQuery worldQuery;
            worldQuery.entity = activeEntity;
            auto worldOpt = dispatcher.query(worldQuery);
            if (!worldOpt.has_value()) return;
            pristine = *worldOpt;
            objectMatrix = grouptransform::composeWorld(pristine);
        }
        else
        {
            events::scene::GetTransformQuery transformQuery;
            transformQuery.entity = activeEntity;
            auto transformOpt = dispatcher.query(transformQuery);
            if (!transformOpt.has_value()) return;
            pristine = *transformOpt;
            objectMatrix = buildTransformMatrix(pristine);
        }
        const glm::mat4 pristineMatrix = objectMatrix;

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

        // Undo Vulkan Y-flip for ImGuizmo (it expects OpenGL-style projection)
        glm::mat4 view = camera.getViewMatrix();
        glm::mat4 proj = camera.getProjectionMatrix();
        proj[1][1] *= -1.0f;

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

        if (usingGizmo && !dragActive)
        {
            beginDrag(selectedEntities, activeEntity, pristine, pristineMatrix, group);
        }

        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                                 op, currentGizmoMode, glm::value_ptr(objectMatrix)))
        {
            if (!dragActive)
            {
                // Movement on the press frame itself — capture the pristine
                // state read above before applying anything.
                beginDrag(selectedEntities, activeEntity, pristine, pristineMatrix, group);
            }

            if (group)
            {
                applyGroupDelta(objectMatrix);
            }
            else
            {
                events::scene::SetTransformCommand cmd;
                cmd.entity = activeEntity;
                cmd.transform = decomposeTransformMatrix(objectMatrix);
                dispatcher.execute(cmd);
            }
        }
    }

    void ViewPortGizmo::beginDrag(const std::vector<services::EntityHandle>& selectedEntities,
                                  services::EntityHandle activeEntity,
                                  const services::TransformData& pristine,
                                  const glm::mat4& pristineMatrix,
                                  bool group)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dragActive = true;
        dragIsGroup = group;
        dragActiveEntity = activeEntity;
        dragStartWorlds.clear();

        if (!group)
        {
            dragStartLocal = pristine;
            return;
        }

        dragStartActiveWorld = pristineMatrix;

        // Only top-level members receive the delta: a selected descendant of a
        // selected ancestor already moves with it (no double transform).
        auto rootHandle = dispatcher.query(events::scene::GetRootEntityQuery{});
        auto parentOf = [&dispatcher](services::EntityHandle handle)
            -> std::optional<services::EntityHandle>
        {
            events::scene::GetEntityQuery entityQuery;
            entityQuery.entity = handle;
            auto data = dispatcher.query(entityQuery);
            if (!data.has_value()) return std::nullopt;
            return data->parent;
        };
        auto topLevel = selection::collectTopLevel(selectedEntities, parentOf, rootHandle);

        dragStartWorlds.reserve(topLevel.size());
        for (const auto& entity : topLevel)
        {
            events::scene::GetWorldTransformQuery worldQuery;
            worldQuery.entity = entity;
            auto world = dispatcher.query(worldQuery);
            if (!world.has_value()) continue; // no TransformComponent — not transformable
            dragStartWorlds.emplace_back(entity, *world);
        }
    }

    void ViewPortGizmo::applyGroupDelta(const glm::mat4& newActiveWorld)
    {
        auto deltaOpt = grouptransform::worldDelta(dragStartActiveWorld, newActiveWorld);
        if (!deltaOpt.has_value()) return;

        auto& dispatcher = events::EventDispatcher::instance();
        for (const auto& [entity, startWorld] : dragStartWorlds)
        {
            auto newWorld = grouptransform::applyWorldDelta(*deltaOpt, startWorld);
            if (!newWorld.has_value()) continue;

            events::scene::SetWorldTransformCommand cmd;
            cmd.entity = entity;
            cmd.worldTransform = *newWorld;
            dispatcher.execute(cmd);
        }
    }

    void ViewPortGizmo::finalizeDrag()
    {
        dragActive = false;
        auto& dispatcher = events::EventDispatcher::instance();

        if (!dragIsGroup)
        {
            events::scene::GetTransformQuery afterQuery;
            afterQuery.entity = dragActiveEntity;
            auto after = dispatcher.query(afterQuery);
            if (after.has_value() && !(dragStartLocal == *after))
            {
                events::undoredo::PushUndoableCommand push;
                push.command = std::make_shared<SceneEntityTransformUndoCommand>(
                    dragActiveEntity, dragStartLocal, *after, false, "Transform Entity");
                dispatcher.execute(push);
            }
            return;
        }

        std::vector<std::shared_ptr<services::IUndoableCommand>> commands;
        commands.reserve(dragStartWorlds.size());
        for (const auto& [entity, beforeWorld] : dragStartWorlds)
        {
            events::scene::GetWorldTransformQuery afterQuery;
            afterQuery.entity = entity;
            auto after = dispatcher.query(afterQuery);
            if (!after.has_value()) continue; // deleted mid-drag — nothing to record
            if (beforeWorld == *after) continue;
            commands.push_back(std::make_shared<SceneEntityTransformUndoCommand>(
                entity, beforeWorld, *after, true, "Transform Entities"));
        }
        dragStartWorlds.clear();

        if (commands.empty()) return;

        if (commands.size() == 1)
        {
            events::undoredo::PushUndoableCommand push;
            push.command = std::move(commands.front());
            dispatcher.execute(push);
            return;
        }

        // One undo/redo operation for the whole group manipulation.
        events::undoredo::BeginBatchCommand begin;
        begin.description = "Transform Entities";
        dispatcher.execute(begin);
        for (auto& command : commands)
        {
            events::undoredo::PushUndoableCommand push;
            push.command = std::move(command);
            dispatcher.execute(push);
        }
        dispatcher.execute(events::undoredo::EndBatchCommand{});
    }

    glm::mat4 ViewPortGizmo::buildTransformMatrix(const services::TransformData& transform) const
    {
        float translation[3] = {transform.position.x, transform.position.y, transform.position.z};
        float rotation[3] = {transform.rotation.x, transform.rotation.y, transform.rotation.z};
        float scale[3] = {transform.scale.x, transform.scale.y, transform.scale.z};

        glm::mat4 mat(1.0f);
        ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, glm::value_ptr(mat));
        return mat;
    }

    services::TransformData ViewPortGizmo::decomposeTransformMatrix(const glm::mat4& matrix) const
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
