#include "NavRouteDrawer.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include "components/Components.hpp"
#include <imgui.h>
#include <string>
#include <vector>

namespace windows::details
{
    bool NavRouteDrawer::draw(services::EntityHandle handle)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = services::internal::fromHandle(handle);

        if (!registry.valid(entity) || !registry.all_of<components::NavRouteComponent>(entity))
        {
            return false;
        }

        auto& route = registry.get<components::NavRouteComponent>(entity);

        bool open = true;
        if (ImGui::CollapsingHeader("Nav Route", &open, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            // The markers are the entity's children, in order; FollowRoute walks their world positions.
            std::vector<entt::entity> markers;
            if (const auto* children = registry.try_get<components::ChildrenComponent>(entity))
            {
                for (entt::entity child : children->children)
                {
                    if (registry.valid(child)) markers.push_back(child);
                }
            }

            if (markers.empty())
            {
                ImGui::Text("Markers: 0");
                ImGui::TextDisabled("Add child entities as the route's markers");
            }
            else
            {
                const std::string header = "Markers (" + std::to_string(markers.size()) + ")###NavRouteMarkers";
                const bool listOpen = ImGui::TreeNode(header.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("The route's markers are this entity's children, in hierarchy order");
                if (listOpen)
                {
                    for (size_t i = 0; i < markers.size(); ++i)
                    {
                        const auto* name = registry.try_get<components::NameComponent>(markers[i]);
                        const char* label = name && !name->name.empty() ? name->name.c_str() : "(unnamed)";
                        ImGui::Text("%d  %s", static_cast<int>(i), label);
                    }
                    ImGui::TreePop();
                }
            }

            ImGui::Checkbox("Show Path##NavRouteShowPath", &route.showPath);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Draw the route while navmesh debug drawing is on");

            ImGui::Unindent();
        }

        if (!open)
        {
            events::scene::RemoveNavRouteComponentCommand cmd;
            cmd.entity = handle;
            events::EventDispatcher::instance().execute(cmd);
            return false;
        }

        return true;
    }
}
