#pragma once
#include "data/DTOs.hpp"
#include <imgui.h>
#include "ImGuizmo.h"
#include <glm/glm.hpp>

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
    };
}
