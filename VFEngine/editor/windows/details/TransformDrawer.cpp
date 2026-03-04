#include "TransformDrawer.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include <imgui.h>

namespace windows::details {

    void TransformDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::GetTransformQuery transformQuery;
        transformQuery.entity = handle;
        auto transformOpt = dispatcher.query(transformQuery);

        if (!transformOpt.has_value())
            return;

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            services::TransformData transform = *transformOpt;
            bool changed = false;

            changed |= ImGui::DragFloat3("Position", &transform.position.x, 0.1f);
            changed |= ImGui::DragFloat3("Rotation", &transform.rotation.x, 0.1f);
            changed |= ImGui::DragFloat3("Scale", &transform.scale.x, 0.1f);

            if (changed)
            {
                events::scene::SetTransformCommand cmd;
                cmd.entity = handle;
                cmd.transform = transform;
                dispatcher.execute(cmd);
            }

            // Static flag - affects BVH placement, physics, and lights
            events::scene::IsEntityStaticQuery staticQuery;
            staticQuery.entity = handle;
            bool isStatic = dispatcher.query(staticQuery);

            if (ImGui::Checkbox("Is Static", &isStatic))
            {
                events::scene::SetEntityStaticCommand cmd;
                cmd.entity = handle;
                cmd.isStatic = isStatic;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Static entities are placed in a BVH that rebuilds less frequently.\n"
                    "Uncheck for entities that move often (affects rendering, physics, and lights).");
            }
        }
    }

}
