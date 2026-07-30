#include "TerrainTileDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/TerrainEvents.hpp"
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

            ImGui::Separator();

            ImGui::Text("Height Range: %.2f to %.2f", tile.boundingMinY, tile.boundingMaxY);

            // VK-1613: "Visible", "Dirty" and "GPU Resident" used to be printed here and none of them
            // was live. TerrainTileComponent::isVisible/isDirty are a creation-time snapshot
            // (TerrainCreationOps writes them once and nothing refreshes them) and isGPUResident's only
            // write in the repo is `= false`. The live state lives on terrain::TerrainTile, which this
            // component has no link to — the tile entity does not record its parent terrain — so these
            // are removed rather than wired. The terrain's own panel reports the real aggregates.

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        return true;
    }

}
