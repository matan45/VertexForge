#include "TerrainTileDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/TerrainEvents.hpp"
#include <imgui.h>

namespace windows::details {

    bool TerrainTileDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::terrain::HasTerrainTileComponentQuery hasTileQuery;
        hasTileQuery.entity = handle;
        bool hasTile = dispatcher.query(hasTileQuery);

        if (!hasTile)
            return false;

        events::terrain::GetTerrainTileDataQuery tileQuery;
        tileQuery.entity = handle;
        auto tileOpt = dispatcher.query(tileQuery);

        if (!tileOpt.has_value())
            return true;

        ImGui::PushID("TerrainTileComponent");

        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##TerrainTileHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Terrain Tile");

        EntityDetailsPanel::popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            const auto& tile = *tileOpt;

            ImGui::Text("Coordinates: (%d, %d)", tile.tileX, tile.tileZ);
            ImGui::Text("Current LOD: %u", tile.currentLOD);
            ImGui::Text("Visible: %s", tile.isVisible ? "Yes" : "No");

            ImGui::Separator();

            ImGui::Text("Dirty: %s", tile.isDirty ? "Yes" : "No");
            ImGui::Text("WeightMap Dirty: %s", tile.isWeightMapDirty ? "Yes" : "No");
            ImGui::Text("GPU Resident: %s", tile.isGPUResident ? "Yes" : "No");

            ImGui::Separator();

            ImGui::Text("Height Range: %.2f to %.2f", tile.boundingMinY, tile.boundingMaxY);

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        return true;
    }

}
