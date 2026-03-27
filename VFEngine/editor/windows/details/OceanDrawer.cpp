#include "OceanDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/OceanEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool OceanDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ocean::HasOceanComponentQuery hasOceanQuery;
        hasOceanQuery.entity = handle;
        bool hasOcean = dispatcher.query(hasOceanQuery);

        if (!hasOcean)
            return false;

        events::ocean::GetOceanDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
            return true;

        const auto& data = *dataOpt;

        ImGui::PushID("OceanComponent");

        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##OceanHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Ocean");

        EntityDetailsPanel::popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            ImGui::Text("Water Height: %.2f", data.waterHeight);
            ImGui::Text("Physics: %s", data.physicsEnabled ? "Enabled" : "Disabled");

            ImGui::Separator();
            ImGui::Text("Ocean FFT");
            ImGui::Text("  Resolution: %u", data.oceanConfig.resolution);
            ImGui::Text("  Patch Size: %.0f", data.oceanConfig.patchSize);
            ImGui::Text("  Wind Speed: %.2f m/s", data.oceanConfig.windSpeed);
            ImGui::Text("  Choppiness: %.2f", data.oceanConfig.choppiness);

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        return true;
    }
}
