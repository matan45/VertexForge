#include "WaterDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/WaterEvents.hpp"
#include <imgui.h>

namespace windows::details {

    bool WaterDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::water::HasWaterComponentQuery hasWaterQuery;
        hasWaterQuery.entity = handle;
        bool hasWater = dispatcher.query(hasWaterQuery);

        if (!hasWater)
            return false;

        events::water::GetWaterDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
            return true;

        const auto& data = *dataOpt;

        ImGui::PushID("WaterComponent");

        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##WaterHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Water");

        EntityDetailsPanel::popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            ImGui::Text("Tile Size: %.1f units", data.worldTileSize);

            int gridWidth = data.gridMaxX - data.gridMinX + 1;
            int gridDepth = data.gridMaxZ - data.gridMinZ + 1;
            ImGui::Text("Grid: %d x %d tiles", gridWidth, gridDepth);
            ImGui::Text("Tile Count: %u", data.tileCount);
            ImGui::Text("Water Height: %.2f", data.defaultWaterHeight);
            ImGui::Text("Physics: %s", data.physicsEnabled ? "Enabled" : "Disabled");

            ImGui::Separator();
            ImGui::Text("Grid Expansion");

            ImGui::InputInt("Tile X", &pendingTileX);
            ImGui::InputInt("Tile Z", &pendingTileZ);

            if (ImGui::Button("Add Tile"))
            {
                events::water::AddWaterTileCommand cmd;
                cmd.waterEntity = handle;
                cmd.tileX = pendingTileX;
                cmd.tileZ = pendingTileZ;
                bool result = dispatcher.execute(cmd);
                if (!result)
                {
                    statusMessage = "Tile already exists or add failed";
                    statusFrameCounter = 180;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove Tile"))
            {
                events::water::RemoveWaterTileCommand cmd;
                cmd.waterEntity = handle;
                cmd.tileX = pendingTileX;
                cmd.tileZ = pendingTileZ;
                bool result = dispatcher.execute(cmd);
                if (!result)
                {
                    statusMessage = "Tile not found or remove failed";
                    statusFrameCounter = 180;
                }
            }

            if (!statusMessage.empty())
            {
                statusFrameCounter--;
                if (statusFrameCounter <= 0)
                {
                    statusMessage.clear();
                }
                else
                {
                    bool isError = statusMessage.find("failed") != std::string::npos;
                    ImVec4 color = isError
                        ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
                        : ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
                    ImGui::TextColored(color, "%s", statusMessage.c_str());
                }
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        return true;
    }

}
