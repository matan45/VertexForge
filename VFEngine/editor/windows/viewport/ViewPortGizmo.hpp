#pragma once
#include "data/DTOs.hpp"
#include "data/EntityHandle.hpp"
#include <imgui.h>
#include "ImGuizmo.h"
#include <glm/glm.hpp>
#include <utility>
#include <vector>

namespace editor
{
    class EditorCamera;
}

namespace windows
{
    enum class GizmoOperation
    {
        None,
        Translate,
        Rotate,
        Scale
    };

    class ViewPortGizmo
    {
    private:
        GizmoOperation currentGizmoOp = GizmoOperation::None;
        ImGuizmo::MODE currentGizmoMode = ImGuizmo::LOCAL;

        // VK-1490 drag capture: the ImGuizmo::IsUsing() rising edge snapshots
        // the pre-drag state, the falling edge coalesces the whole drag into
        // ONE undo entry (a batch for group drags). Group drags manipulate the
        // ACTIVE entity's world matrix and re-apply its delta to every selected
        // top-level entity, keeping the group rigid about the active pivot.
        bool dragActive = false;
        bool dragIsGroup = false;
        services::EntityHandle dragActiveEntity;
        services::TransformData dragStartLocal;  // single-entity path (local space)
        glm::mat4 dragStartActiveWorld{1.0f};    // group path pivot matrix
        std::vector<std::pair<services::EntityHandle, services::TransformData>> dragStartWorlds;

    public:
        void draw(const editor::EditorCamera& camera);

        GizmoOperation getOperation() const { return currentGizmoOp; }
        void setOperation(GizmoOperation op) { currentGizmoOp = op; }

        ImGuizmo::MODE getMode() const { return currentGizmoMode; }
        void setMode(ImGuizmo::MODE mode) { currentGizmoMode = mode; }

        void toggleMode()
        {
            currentGizmoMode = (currentGizmoMode == ImGuizmo::WORLD) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
        }

        void toggleOperation(GizmoOperation op)
        {
            currentGizmoOp = (currentGizmoOp == op) ? GizmoOperation::None : op;
        }

    private:
        glm::mat4 buildTransformMatrix(const services::TransformData& transform) const;
        services::TransformData decomposeTransformMatrix(const glm::mat4& matrix) const;

        void beginDrag(const std::vector<services::EntityHandle>& selectedEntities,
                       services::EntityHandle activeEntity,
                       const services::TransformData& pristine,
                       const glm::mat4& pristineMatrix,
                       bool group);
        void applyGroupDelta(const glm::mat4& newActiveWorld);
        void finalizeDrag();
    };
}
