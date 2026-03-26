#include "NavmeshModifierVolumeDrawer.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include "components/Components.hpp"
#include "types/NavmeshTypes.hpp"
#include <imgui.h>

namespace windows::details
{
    static const char* modifierShapeNames[] = {"Box", "Cylinder"};

    bool NavmeshModifierVolumeDrawer::draw(services::EntityHandle handle)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = services::internal::fromHandle(handle);

        if (!registry.valid(entity) || !registry.all_of<components::NavmeshModifierVolumeComponent>(entity))
        {
            return false;
        }

        auto& volume = registry.get<components::NavmeshModifierVolumeComponent>(entity);

        bool open = true;
        if (ImGui::CollapsingHeader("Navmesh Modifier Volume", &open, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            ImGui::Text("Shape");
            ImGui::PushItemWidth(-1);
            int shapeIdx = static_cast<int>(volume.shape);
            if (ImGui::Combo("##NavModVolShape", &shapeIdx, modifierShapeNames, IM_ARRAYSIZE(modifierShapeNames)))
            {
                volume.shape = static_cast<components::NavmeshModifierVolumeShape>(shapeIdx);
            }
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Text("Size");
            ImGui::PushItemWidth(-1);
            if (volume.shape == components::NavmeshModifierVolumeShape::Box)
            {
                ImGui::DragFloat3("##NavModVolSize", &volume.size.x, 0.1f, 0.1f, 100.0f, "%.1f");
            }
            else
            {
                ImGui::DragFloat("##NavModVolRadius", &volume.size.x, 0.01f, 0.1f, 50.0f, "Radius: %.2f");
                ImGui::DragFloat("##NavModVolHeight", &volume.size.y, 0.1f, 0.1f, 50.0f, "Height: %.1f");
            }
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Text("Offset");
            ImGui::PushItemWidth(-1);
            ImGui::DragFloat3("##NavModVolOffset", &volume.offset.x, 0.1f, -100.0f, 100.0f, "%.1f");
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Text("Area Type");
            ImGui::PushItemWidth(-1);

            // Build area type names for the dropdown (named areas 0-9)
            int areaIdx = static_cast<int>(volume.areaType);
            if (areaIdx >= types::NAVMESH_NAMED_AREA_COUNT)
                areaIdx = 0;

            const char* areaNames[types::NAVMESH_NAMED_AREA_COUNT];
            for (int i = 0; i < types::NAVMESH_NAMED_AREA_COUNT; ++i)
                areaNames[i] = types::getNavmeshAreaName(static_cast<uint8_t>(i));

            if (ImGui::Combo("##NavModVolArea", &areaIdx, areaNames, types::NAVMESH_NAMED_AREA_COUNT))
            {
                volume.areaType = static_cast<uint8_t>(areaIdx);
            }
            ImGui::PopItemWidth();

            ImGui::Unindent();
        }

        if (!open)
        {
            events::scene::RemoveNavmeshModifierVolumeComponentCommand cmd;
            cmd.entity = handle;
            events::EventDispatcher::instance().execute(cmd);
            return false;
        }

        return true;
    }
}
