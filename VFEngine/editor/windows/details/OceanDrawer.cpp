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
            ImGui::Text("Sea State: Beaufort %.1f%s", data.currentBeaufort,
                        data.weatherDriven ? " (weather driven)" : "");

            ImGui::Separator();
            ImGui::Text("Ocean FFT Bands");
            static const char* bandNames[] = {"Swell", "Agitation", "Ripples"};
            for (uint32_t i = 0; i < 3; ++i)
            {
                const auto& band = data.oceanConfig.bands[i];
                if (!band.enabled) continue;
                ImGui::Text("  %s: %ux%u, patch=%.0f, wind=%.1f",
                    bandNames[i], band.resolution, band.resolution, band.patchSize, band.windSpeed);
            }

            // VK-1604
            ImGui::Separator();
            ImGui::Text("SSR: %s", data.ssrEnabled ? "On" : "Off");
            ImGui::Text("Beer-Lambert: %s", data.beerLambertEnabled ? "On" : "Off");
            ImGui::Text("Anti-Tiling: %s", data.hexTilingEnabled ? "On" : "Off");

            // VK-1605
            ImGui::Text("Shoaling: %s", data.shoalingEnabled ? "On" : "Off");
            ImGui::Text("Breaking Waves: %s", data.shoreWavesEnabled ? "On" : "Off");

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        return true;
    }
}
