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

            // VK-1597: World Sector streaming policy. Sits here rather than behind its own
            // Add Component entry because it is the sibling of Is Static above and every entity
            // has a Transform, so it is discoverable without knowing the component exists.
            events::scene::IsEntitySpatiallyLoadedQuery spatialQuery;
            spatialQuery.entity = handle;
            bool spatiallyLoaded = dispatcher.query(spatialQuery);

            if (ImGui::Checkbox("Spatially Loaded", &spatiallyLoaded))
            {
                events::scene::SetEntitySpatiallyLoadedCommand cmd;
                cmd.entity = handle;
                cmd.spatiallyLoaded = spatiallyLoaded;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Uncheck to keep this entity loaded no matter which World Sector it sits in\n"
                    "(global managers, match state, landmark meshes).\n\n"
                    "An always-loaded entity is stored in the SCENE file, not in a .vfsector, so\n"
                    "Save Scene is what persists it - Save World alone is not enough.\n\n"
                    "Only affects sector streaming. Entities with Is Static unchecked already\n"
                    "survive an unload, and terrain / ocean / IBL / cameras are never streamed.");
            }
        }
    }

}
