#include "NavmeshRootDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include <imgui.h>

namespace windows::details {

    void NavmeshRootDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasNavmeshRootQuery hasQuery;
        hasQuery.entity = handle;
        bool hasNavmesh = dispatcher.query(hasQuery);

        if (!hasNavmesh)
            return;

        events::scene::GetNavmeshRootDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
            return;

        ImGui::PushID("NavmeshRootComponent");

        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##NavmeshRootHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Navmesh");
        EntityDetailsPanel::popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);
            ImGui::Text("File: %s", dataOpt->navmeshPath.c_str());
            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();
    }

}
