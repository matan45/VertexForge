#include "WorldSectorWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/world/WorldSectorEvents.hpp"
#include "imgui.h"
#include "nfd/FileDialog.hpp"

namespace
{
    const std::vector<std::pair<std::wstring, std::wstring>> WORLD_FILE_TYPES = {
        {L"World Files", L"*.vfworld"}
    };
}

namespace windows
{
    void WorldSectorWindow::draw()
    {
        if (!visible) return;

        refreshTimer += ImGui::GetIO().DeltaTime;
        if (refreshTimer >= REFRESH_INTERVAL)
        {
            refreshStats();
            refreshTimer = 0.0f;
        }

        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("World Sectors", &visible))
        {
            auto& dispatcher = events::EventDispatcher::instance();
            bool isWorld = dispatcher.query(events::world::IsWorldModeQuery{});

            if (isWorld)
            {
                drawWorldInfo();
                ImGui::Separator();

                if (ImGui::BeginTabBar("WorldSectorTabs"))
                {
                    if (ImGui::BeginTabItem("Sector Grid"))
                    {
                        drawSectorGrid();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Streaming Config"))
                    {
                        drawStreamingConfig();
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }

                ImGui::Separator();
                if (ImGui::Button("Save World"))
                {
                    events::world::SaveWorldCommand cmd;
                    dispatcher.execute(cmd);
                }
                ImGui::SameLine();
                if (ImGui::Button("Load World..."))
                {
                    nfd::FileDialog fileDialog;
                    std::string path = fileDialog.openFileDialog(WORLD_FILE_TYPES);
                    if (!path.empty())
                    {
                        events::world::LoadWorldCommand cmd;
                        cmd.filePath = path;
                        dispatcher.execute(cmd);
                    }
                }
            }
            else
            {
                ImGui::TextDisabled("No world loaded.");
                ImGui::Spacing();

                if (ImGui::Button("Create New World"))
                {
                    showCreationWizard = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Load World..."))
                {
                    nfd::FileDialog fileDialog;
                    std::string path = fileDialog.openFileDialog(WORLD_FILE_TYPES);
                    if (!path.empty())
                    {
                        events::world::LoadWorldCommand cmd;
                        cmd.filePath = path;
                        dispatcher.execute(cmd);
                    }
                }
            }

            if (showCreationWizard)
            {
                drawCreationWizard();
            }
        }
        ImGui::End();
    }

    void WorldSectorWindow::drawWorldInfo()
    {
        ImGui::Text("Sectors: %d total | %d loaded | %d unloaded | %d loading",
                     totalSectors, loadedSectors, unloadedSectors, loadingSectors);
    }

    void WorldSectorWindow::drawSectorGrid()
    {
        ImGui::TextDisabled("Color: Green=Loaded, Gray=Unloaded, Yellow=Loading, Red=Unloading");
        ImGui::Spacing();

        // Simple grid visualization using buttons
        auto& dispatcher = events::EventDispatcher::instance();

        // Find bounds of sectors
        int minX = 0, maxX = 0, minZ = 0, maxZ = 0;
        bool first = true;

        // We iterate a reasonable range. For now, show -8 to +8 range
        int range = 8;
        for (int z = range; z >= -range; --z)
        {
            for (int x = -range; x <= range; ++x)
            {
                world::SectorCoord coord(x, z);
                events::world::GetSectorStateQuery query;
                query.coord = coord;
                auto state = dispatcher.query(query);

                ImVec4 color;
                switch (state)
                {
                case world::SectorState::Loaded:
                    color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
                    break;
                case world::SectorState::Loading:
                    color = ImVec4(0.9f, 0.9f, 0.2f, 1.0f);
                    break;
                case world::SectorState::Unloading:
                    color = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
                    break;
                default:
                    color = ImVec4(0.3f, 0.3f, 0.3f, 0.5f);
                    break;
                }

                ImGui::PushID(x * 1000 + z);
                ImGui::PushStyleColor(ImGuiCol_Button, color);

                char label[32];
                snprintf(label, sizeof(label), "%d,%d", x, z);
                if (ImGui::Button(label, ImVec2(40, 20)))
                {
                    // Manual load/unload toggle
                    if (state == world::SectorState::Loaded)
                    {
                        events::world::UnloadSectorCommand cmd;
                        cmd.coord = coord;
                        dispatcher.execute(cmd);
                    }
                    else if (state == world::SectorState::Unloaded)
                    {
                        events::world::LoadSectorCommand cmd;
                        cmd.coord = coord;
                        dispatcher.execute(cmd);
                    }
                }

                ImGui::PopStyleColor();
                ImGui::PopID();

                if (x < range)
                    ImGui::SameLine();
            }
        }
    }

    void WorldSectorWindow::drawStreamingConfig()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        auto config = dispatcher.query(events::world::GetWorldStreamingStatsQuery{});

        ImGui::Text("Load Radius: %.0f", config.loadRadius);
        ImGui::Text("Unload Radius: %.0f", config.unloadRadius);
        ImGui::Text("Max Loads/Frame: %d", config.maxLoadsPerFrame);
        ImGui::Text("Max Unloads/Frame: %d", config.maxUnloadsPerFrame);
        ImGui::Text("Max Entities/Frame: %d", config.maxEntitiesPerFrame);
    }

    void WorldSectorWindow::drawCreationWizard()
    {
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Create World", &showCreationWizard))
        {
            ImGui::InputText("World Name", worldName, sizeof(worldName));
            ImGui::InputFloat("Sector Size", &sectorSize, 16.0f, 64.0f);
            ImGui::InputInt("Tiles Per Sector", &tilesPerSector);
            ImGui::Separator();
            ImGui::InputFloat("Load Radius", &loadRadius, 32.0f, 128.0f);
            ImGui::InputFloat("Unload Radius", &unloadRadius, 32.0f, 128.0f);

            ImGui::Spacing();
            if (ImGui::Button("Create"))
            {
                nfd::FileDialog fileDialog;
                std::string path = fileDialog.saveFileDialog(WORLD_FILE_TYPES, L"vfworld");
                if (!path.empty())
                {
                    events::world::CreateWorldCommand cmd;
                    cmd.name = worldName;
                    cmd.filePath = path;
                    cmd.sectorConfig.sectorWorldSize = sectorSize;
                    cmd.sectorConfig.tilesPerSector = tilesPerSector;
                    cmd.streamingConfig.loadRadius = loadRadius;
                    cmd.streamingConfig.unloadRadius = unloadRadius;
                    events::EventDispatcher::instance().execute(cmd);
                    showCreationWizard = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                showCreationWizard = false;
            }
        }
        ImGui::End();
    }

    void WorldSectorWindow::refreshStats()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        bool isWorld = dispatcher.query(events::world::IsWorldModeQuery{});
        if (!isWorld)
        {
            totalSectors = loadedSectors = unloadedSectors = loadingSectors = 0;
            return;
        }

        // Count states by querying a reasonable grid range
        totalSectors = 0;
        loadedSectors = 0;
        unloadedSectors = 0;
        loadingSectors = 0;

        int range = 8;
        for (int z = -range; z <= range; ++z)
        {
            for (int x = -range; x <= range; ++x)
            {
                events::world::GetSectorStateQuery query;
                query.coord = world::SectorCoord(x, z);
                auto state = dispatcher.query(query);

                if (state != world::SectorState::Unloaded || true) // Count all sectors in range
                {
                    totalSectors++;
                    switch (state)
                    {
                    case world::SectorState::Loaded: loadedSectors++; break;
                    case world::SectorState::Loading: loadingSectors++; break;
                    default: unloadedSectors++; break;
                    }
                }
            }
        }
    }

} // namespace windows
