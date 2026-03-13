#include "WorldSectorWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/world/WorldSectorEvents.hpp"
#include "imgui.h"
#include "nfd/FileDialog.hpp"

namespace windows
{
    const std::vector<std::pair<std::wstring, std::wstring>> WorldSectorWindow::WORLD_FILE_TYPES = {
        {L"World Files", L"*.vfworld"}
    };
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

                bool debugDraw = dispatcher.query(events::world::GetSectorDebugDrawQuery{});
                if (ImGui::Checkbox("Show Sector Bounds", &debugDraw))
                {
                    events::world::SetSectorDebugDrawCommand cmd;
                    cmd.enabled = debugDraw;
                    dispatcher.execute(cmd);
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
                ImGui::SameLine();
                if (ImGui::Button("Clear World"))
                {
                    events::world::ClearWorldCommand cmd;
                    dispatcher.execute(cmd);
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
        ImGui::TextDisabled("Color: Green=Loaded, Gray=Unloaded, Yellow=Loading");
        ImGui::TextDisabled("Only sectors with saved data are shown as clickable.");
        ImGui::Spacing();

        auto& dispatcher = events::EventDispatcher::instance();
        int range = 8;
        int gridWidth = range * 2 + 1;

        for (size_t i = 0; i < cachedGrid.size(); ++i)
        {
            const auto& info = cachedGrid[i];
            int x = info.coord.x;
            int z = info.coord.z;

            ImGui::PushID(x * 1000 + z);

            if (!info.exists)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 0.3f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.15f, 0.15f, 0.3f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.15f, 0.3f));
                ImGui::Button("##empty", ImVec2(40, 20));
                ImGui::PopStyleColor(3);
            }
            else
            {
                ImVec4 color;
                switch (info.state)
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
                    color = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
                    break;
                }

                ImGui::PushStyleColor(ImGuiCol_Button, color);

                char label[32];
                snprintf(label, sizeof(label), "%d,%d", x, z);
                if (ImGui::Button(label, ImVec2(40, 20)))
                {
                    if (info.state == world::SectorState::Loaded)
                    {
                        events::world::UnloadSectorCommand cmd;
                        cmd.coord = info.coord;
                        dispatcher.execute(cmd);
                    }
                    else if (info.state == world::SectorState::Unloaded)
                    {
                        events::world::LoadSectorCommand cmd;
                        cmd.coord = info.coord;
                        dispatcher.execute(cmd);
                    }
                }

                ImGui::PopStyleColor();
            }

            ImGui::PopID();

            if ((i + 1) % gridWidth != 0)
                ImGui::SameLine();
        }
    }

    void WorldSectorWindow::drawStreamingConfig()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        auto config = dispatcher.query(events::world::GetWorldStreamingStatsQuery{});

        ImGui::Text("Load Radius: %.0f sectors", config.loadRadius);
        ImGui::Text("Unload Radius: %.0f sectors", config.unloadRadius);
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
            ImGui::InputFloat("Load Radius (sectors)", &loadRadius, 1.0f, 2.0f);
            ImGui::InputFloat("Unload Radius (sectors)", &unloadRadius, 1.0f, 2.0f);

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
            cachedGrid.clear();
            return;
        }

        totalSectors = 0;
        loadedSectors = 0;
        unloadedSectors = 0;
        loadingSectors = 0;
        cachedGrid.clear();

        int range = 8;
        for (int z = range; z >= -range; --z)
        {
            for (int x = -range; x <= range; ++x)
            {
                CachedSectorInfo info;
                info.coord = world::SectorCoord(x, z);

                events::world::DoesSectorExistQuery existQuery;
                existQuery.coord = info.coord;
                info.exists = dispatcher.query(existQuery);

                if (info.exists)
                {
                    events::world::GetSectorStateQuery stateQuery;
                    stateQuery.coord = info.coord;
                    info.state = dispatcher.query(stateQuery);
                }

                cachedGrid.push_back(info);

                totalSectors++;
                switch (info.state)
                {
                case world::SectorState::Loaded: loadedSectors++; break;
                case world::SectorState::Loading: loadingSectors++; break;
                default: unloadedSectors++; break;
                }
            }
        }
    }

} // namespace windows
