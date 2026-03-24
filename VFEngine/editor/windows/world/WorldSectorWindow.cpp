#include "WorldSectorWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/world/WorldSectorEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "events/render/ObjectStreamingEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "imgui.h"
#include <cmath>
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
                    terrainTileSize = events::EventDispatcher::instance().query(
                        events::terrain::GetActiveTerrainTileSizeQuery{});
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
        // Navigation controls
        if (ImGui::Checkbox("Follow Camera", &followCamera))
        {
            if (followCamera)
            {
                gridCenterX = cameraSectorX;
                gridCenterZ = cameraSectorZ;
            }
        }

        if (!followCamera)
        {
            ImGui::SameLine();
            if (ImGui::ArrowButton("##left", ImGuiDir_Left)) { --gridCenterX; }
            ImGui::SameLine();
            if (ImGui::ArrowButton("##right", ImGuiDir_Right)) { ++gridCenterX; }
            ImGui::SameLine();
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) { ++gridCenterZ; }
            ImGui::SameLine();
            if (ImGui::ArrowButton("##down", ImGuiDir_Down)) { --gridCenterZ; }
            ImGui::SameLine();
            ImGui::Text("Center: (%d, %d)", gridCenterX, gridCenterZ);
        }

        ImGui::TextDisabled("Color: Green=Loaded, Gray=Unloaded, Yellow=Loading, Blue outline=Camera");
        ImGui::Spacing();

        auto& dispatcher = events::EventDispatcher::instance();
        int gridWidth = gridRange * 2 + 1;

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

                bool isCameraSector = (x == cameraSectorX && z == cameraSectorZ);

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

                if (ImGui::IsItemHovered())
                {
                    int tps = cachedTilesPerSector;
                    const char* stateStr = "Unloaded";
                    if (info.state == world::SectorState::Loaded) stateStr = "Loaded";
                    else if (info.state == world::SectorState::Loading) stateStr = "Loading";
                    else if (info.state == world::SectorState::Unloading) stateStr = "Unloading";

                    ImGui::SetTooltip("Sector (%d, %d) - %s\n"
                                      "Terrain tiles: (%d,%d) to (%d,%d)\n"
                                      "Click to %s",
                                      x, z, stateStr,
                                      x * tps, z * tps,
                                      (x + 1) * tps - 1, (z + 1) * tps - 1,
                                      info.state == world::SectorState::Loaded ? "unload" : "load");
                }

                // Draw blue outline for camera sector
                if (isCameraSector)
                {
                    ImVec2 btnMin = ImGui::GetItemRectMin();
                    ImVec2 btnMax = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddRect(
                        btnMin, btnMax,
                        IM_COL32(80, 140, 255, 255), 0.0f, 0, 2.0f);
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

        ImGui::Separator();
        ImGui::Text("Terrain Tile Streaming (via Sector)");
        ImGui::Text("Max Terrain Loads/Frame: %d", config.maxTerrainLoadsPerFrame);
        ImGui::Text("Max Terrain Unloads/Frame: %d", config.maxTerrainUnloadsPerFrame);

        ImGui::Separator();
        ImGui::Text("GPU Object Streaming: %s", config.enableGPUObjectStreaming ? "Enabled" : "Disabled");

        if (config.enableGPUObjectStreaming)
        {
            auto stats = dispatcher.query(events::render::objectstreaming::GetObjectStreamingStatsQuery{});
            ImGui::Text("Registered: %u | Active on GPU: %u | Queued: %u",
                         stats.totalRegistered, stats.activeOnGPU, stats.queuedForUpload);
            ImGui::Text("Uploads/Frame: %u | Evictions/Frame: %u",
                         stats.uploadsThisFrame, stats.evictionsThisFrame);
            ImGui::Text("Slot Utilization: %.1f%%", stats.slotUtilization * 100.0f);
        }
    }

    void WorldSectorWindow::drawCreationWizard()
    {
        ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Create World", &showCreationWizard))
        {
            ImGui::InputText("World Name", worldName, sizeof(worldName));

            if (terrainTileSize > 0.0f)
            {
                ImGui::Checkbox("Align to Terrain Grid", &autoAlignToTerrain);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Derive sector size from terrain tile size.\nsectorWorldSize = tilesPerSector * worldTileSize");
            }

            ImGui::InputInt("Tiles Per Sector", &tilesPerSector);
            if (tilesPerSector < 1) tilesPerSector = 1;

            if (autoAlignToTerrain && terrainTileSize > 0.0f)
            {
                sectorSize = terrainTileSize * static_cast<float>(tilesPerSector);
                ImGui::BeginDisabled();
                ImGui::InputFloat("Sector Size (auto)", &sectorSize);
                ImGui::EndDisabled();
                ImGui::TextDisabled("= %d tiles x %.0f tile size", tilesPerSector, terrainTileSize);
            }
            else
            {
                ImGui::InputFloat("Sector Size", &sectorSize, 16.0f, 64.0f);
            }

            ImGui::Separator();
            ImGui::InputFloat("Load Radius (sectors)", &loadRadius, 1.0f, 2.0f);
            ImGui::InputFloat("Unload Radius (sectors)", &unloadRadius, 1.0f, 2.0f);
            ImGui::Checkbox("GPU Object Streaming", &gpuObjectStreaming);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Stream GPU objects in/out based on camera distance.\nReduces GPU memory for large worlds with >65K objects.");

            ImGui::Spacing();
            if (ImGui::Button("Create"))
            {
                nfd::FileDialog fileDialog;
                std::string path = fileDialog.saveFileDialog(WORLD_FILE_TYPES, L"vfworld");
                if (!path.empty())
                {
                    bool aligned = autoAlignToTerrain && terrainTileSize > 0.0f;
                    events::world::CreateWorldCommand cmd;
                    cmd.name = worldName;
                    cmd.filePath = path;
                    cmd.sectorConfig.sectorWorldSize = sectorSize;
                    cmd.sectorConfig.tilesPerSector = tilesPerSector;
                    cmd.sectorConfig.alignedToTerrain = aligned;
                    cmd.streamingConfig.loadRadius = loadRadius;
                    cmd.streamingConfig.unloadRadius = unloadRadius;
                    cmd.streamingConfig.enableGPUObjectStreaming = gpuObjectStreaming;
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

        auto sectorConfig = dispatcher.query(events::world::GetSectorConfigQuery{});
        cachedTilesPerSector = sectorConfig.tilesPerSector;

        // Compute camera sector from editor camera position
        {
            events::world::GetSectorAtPositionQuery posQuery;
            posQuery.position = glm::vec3(0.0f); // fallback
            // Use the scene's primary camera or editor camera
            auto cameraOpt = dispatcher.query(events::scene::GetPrimaryCameraQuery{});
            if (cameraOpt.has_value())
            {
                events::scene::GetWorldTransformQuery transformQuery;
                transformQuery.entity = *cameraOpt;
                auto transformOpt = dispatcher.query(transformQuery);
                if (transformOpt.has_value())
                    posQuery.position = transformOpt->position;
            }

            float sectorSize = sectorConfig.sectorWorldSize;
            if (sectorSize > 0.0f)
            {
                cameraSectorX = static_cast<int>(std::floor(posQuery.position.x / sectorSize));
                cameraSectorZ = static_cast<int>(std::floor(posQuery.position.z / sectorSize));
            }

            if (followCamera)
            {
                gridCenterX = cameraSectorX;
                gridCenterZ = cameraSectorZ;
            }
        }

        totalSectors = 0;
        loadedSectors = 0;
        unloadedSectors = 0;
        loadingSectors = 0;
        cachedGrid.clear();

        for (int z = gridCenterZ + gridRange; z >= gridCenterZ - gridRange; --z)
        {
            for (int x = gridCenterX - gridRange; x <= gridCenterX + gridRange; ++x)
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
