#include "LightmapRootDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include <imgui.h>

namespace windows::details {

    void LightmapRootDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasLightmapRootQuery hasQuery;
        hasQuery.entity = handle;
        bool hasLightmap = dispatcher.query(hasQuery);

        if (!hasLightmap)
            return;

        events::scene::GetLightmapRootDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
            return;

        ImGui::PushID("LightmapRootComponent");

        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##LightmapRootHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Lightmap");
        EntityDetailsPanel::popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);
            ImGui::Text("File: %s", dataOpt->lightmapPath.c_str());
            ImGui::Text("Texels/Unit: %.1f", dataOpt->texelsPerUnit);
            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();
    }

}
