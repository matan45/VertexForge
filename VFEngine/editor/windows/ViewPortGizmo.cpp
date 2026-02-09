#include "ViewPortGizmo.hpp"
#include "../camera/EditorCamera.hpp"
#include "events/EventDispatcher.hpp"
#include "events/EditorModeEvents.hpp"
#include "events/SculptModeEvents.hpp"
#include "events/SceneEvents.hpp"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

namespace windows
{
    void ViewPortGizmo::draw(const editor::EditorCamera& camera)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Skip in play mode, sculpt mode, or if no operation selected
        if (dispatcher.query(events::editor::IsPlayModeQuery{})) return;
        if (dispatcher.query(events::sculpt::IsSculptModeActiveQuery{})) return;
        if (currentGizmoOp == GizmoOperation::None) return;

        auto selectedEntity = dispatcher.query(events::scene::GetSelectedEntityQuery{});
        if (!selectedEntity.has_value()) return;

        events::scene::GetTransformQuery transformQuery;
        transformQuery.entity = *selectedEntity;
        auto transformOpt = dispatcher.query(transformQuery);
        if (!transformOpt.has_value()) return;

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
        glm::mat4 objectMatrix = buildTransformMatrix(*transformOpt);

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

        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                                 op, currentGizmoMode, glm::value_ptr(objectMatrix)))
        {
            events::scene::SetTransformCommand cmd;
            cmd.entity = *selectedEntity;
            cmd.transform = decomposeTransformMatrix(objectMatrix);
            dispatcher.execute(cmd);
        }
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
