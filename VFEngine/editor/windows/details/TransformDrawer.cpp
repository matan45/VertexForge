#include "TransformDrawer.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/world/WorldSectorEvents.hpp" // VK-1599: GetWorldGridsQuery
#include <imgui.h>
#include <vector>

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

            // VK-1599: which named runtime grid this entity streams on. Only offered when the open
            // world actually declares more than one - a single-grid world has no choice to make,
            // and showing a one-entry combo would just be noise. Hidden entirely while the entity
            // is pinned always-loaded, because a pinned entity is in no grid at all.
            if (spatiallyLoaded)
            {
                const auto grids = dispatcher.query(events::world::GetWorldGridsQuery{});
                if (grids.size() > 1)
                {
                    events::scene::GetEntityStreamingGridQuery gridQuery;
                    gridQuery.entity = handle;
                    const uint8_t current = dispatcher.query(gridQuery);

                    // Clamped for display: a scene can name a grid the world has since lost, and
                    // the resolver treats that as the primary grid - so show what will actually
                    // happen rather than an index that no longer exists.
                    const int currentItem = current < grids.size() ? static_cast<int>(current) : 0;

                    std::vector<const char*> labels;
                    labels.reserve(grids.size());
                    for (const auto& grid : grids)
                        labels.push_back(grid.name.c_str());

                    int selected = currentItem;
                    if (ImGui::Combo("Streaming Grid", &selected, labels.data(),
                                     static_cast<int>(labels.size())))
                    {
                        events::scene::SetEntityStreamingGridCommand cmd;
                        cmd.entity = handle;
                        cmd.gridIndex = static_cast<uint8_t>(selected);
                        dispatcher.execute(cmd);
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip(
                            "Which World Sector grid streams this entity. Each grid has its own\n"
                            "cell size and load radius, so put short-range clutter on a small-cell\n"
                            "grid and long-range landmarks on a coarse one.\n\n"
                            "The Default grid is the only one that drives terrain, water, navmesh\n"
                            "and HLOD - entities on the others stream by themselves.\n\n"
                            "Changing this moves the entity between sector files on the next\n"
                            "Save World.");
                    }
                }
            }
        }
    }

}
