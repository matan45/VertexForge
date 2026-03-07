#include "WaterTileDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/WaterEvents.hpp"
#include <imgui.h>

namespace windows::details {

    bool WaterTileDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::water::HasWaterTileComponentQuery hasTileQuery;
        hasTileQuery.entity = handle;
        bool hasTile = dispatcher.query(hasTileQuery);

        if (!hasTile)
            return false;

        events::water::GetWaterTileDataQuery tileQuery;
        tileQuery.entity = handle;
        auto tileOpt = dispatcher.query(tileQuery);

        if (!tileOpt.has_value())
            return true;

        ImGui::PushID("WaterTileComponent");

        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##WaterTileHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Water Tile");

        EntityDetailsPanel::popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            const auto& tile = *tileOpt;

            ImGui::Text("Coordinates: (%d, %d)", tile.tileX, tile.tileZ);
            ImGui::Text("Water Height: %.2f", tile.waterHeight);
            ImGui::Text("Wave Intensity: %.2f", tile.waveIntensity);

            ImGui::Separator();

            ImGui::Text("Visible: %s", tile.isVisible ? "Yes" : "No");
            ImGui::Text("Physics: %s", tile.physicsEnabled ? "Enabled" : "Disabled");

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        return true;
    }

}
