#include "TerrainDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/TerrainEvents.hpp"
#include <imgui.h>

namespace windows::details {

    bool TerrainDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::terrain::HasTerrainComponentQuery hasTerrainQuery;
        hasTerrainQuery.entity = handle;
        bool hasTerrain = dispatcher.query(hasTerrainQuery);

        if (!hasTerrain)
            return false;

        events::terrain::GetTerrainDataQuery terrainQuery;
        terrainQuery.entity = handle;
        auto terrainOpt = dispatcher.query(terrainQuery);

        if (!terrainOpt.has_value())
            return true;

        ImGui::PushID("TerrainComponent");

        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##TerrainHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Terrain");

        EntityDetailsPanel::popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            const auto& terrain = *terrainOpt;

            // Resolution
            const char* resolutionNames[] = { "Low (33x33)", "Medium (65x65)", "High (129x129)", "Ultra (257x257)" };
            int resIndex = static_cast<int>(terrain.resolution);
            if (resIndex >= 0 && resIndex < 4)
            {
                ImGui::Text("Resolution: %s", resolutionNames[resIndex]);
            }

            // Grid info
            ImGui::Text("Tile Size: %.1f units", terrain.worldTileSize);
            ImGui::Text("Height Range: %.1f to %.1f", terrain.minHeight, terrain.maxHeight);

            int gridWidth = terrain.gridMaxX - terrain.gridMinX + 1;
            int gridDepth = terrain.gridMaxZ - terrain.gridMinZ + 1;
            ImGui::Text("Grid: %d x %d tiles", gridWidth, gridDepth);
            ImGui::Text("Tile Count: %u", terrain.tileCount);

            ImGui::Separator();

            // State flags
            ImGui::Text("Active: %s", terrain.isActive ? "Yes" : "No");
            ImGui::Text("Dirty: %s", terrain.isDirty ? "Yes" : "No");

            ImGui::Separator();

            // Runtime stats
            ImGui::Text("Active Tiles: %u", terrain.activeTileCount);
            ImGui::Text("Visible Tiles: %u", terrain.visibleTileCount);

            // Heightmap path
            if (!terrain.heightmapPath.empty())
            {
                ImGui::Separator();
                ImGui::Text("Heightmap:");
                ImGui::TextWrapped("%s", terrain.heightmapPath.c_str());
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        return true;
    }

}
