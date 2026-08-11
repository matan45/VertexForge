#include "WorldSectorWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/world/WorldSectorEvents.hpp"
#include "events/world/HLODEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "events/render/ObjectStreamingEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "imgui.h"
#include <algorithm>
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
                    if (ImGui::BeginTabItem("HLOD"))
                    {
                        drawHLODConfig();
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
        ImGui::Text("Sectors: %d total | %d loaded | %d prefetched | %d unloaded | %d loading",
                     totalSectors, loadedSectors, prefetchedSectors, unloadedSectors, loadingSectors);

        uint32_t terrainPending = events::EventDispatcher::instance().query(
            events::terrain::GetPendingSectorTileActionCountQuery{});
        if (terrainPending > 0)
            ImGui::Text("Terrain tile actions pending: %u", terrainPending);
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

        ImGui::TextDisabled("Color: Green=Loaded, Cyan=Prefetched, Gray=Unloaded, Yellow=Loading, "
                            "Blue outline=Camera");
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
                case world::SectorState::Prefetching:
                    color = ImVec4(0.2f, 0.6f, 0.7f, 1.0f); // VK-1591: bytes in flight
                    break;
                case world::SectorState::Prefetched:
                    color = ImVec4(0.2f, 0.8f, 0.9f, 1.0f); // VK-1591: bytes resident, no entities
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
                    // VK-1591: a prefetched sector activates from its cached bytes, so the click
                    // is the same LoadSectorCommand — it just costs no file read.
                    else if (info.state == world::SectorState::Unloaded ||
                             info.state == world::SectorState::Prefetching ||
                             info.state == world::SectorState::Prefetched)
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
                    else if (info.state == world::SectorState::Prefetching) stateStr = "Prefetching (bytes in flight, no entities)";
                    else if (info.state == world::SectorState::Prefetched) stateStr = "Prefetched (bytes resident, no entities)";

                    const char* clickAction = "load";
                    if (info.state == world::SectorState::Loaded) clickAction = "unload";
                    else if (info.state == world::SectorState::Prefetched) clickAction = "activate (no file read)";

                    events::world::GetSectorReadinessQuery readinessQuery;
                    readinessQuery.coord = info.coord;
                    auto readiness = dispatcher.query(readinessQuery);

                    char pendingStr[64] = "ready";
                    if (readiness.fileLoadPending || readiness.entitySpawnsPending)
                    {
                        snprintf(pendingStr, sizeof(pendingStr), "%s%s%s",
                                 readiness.fileLoadPending ? "file I/O" : "",
                                 (readiness.fileLoadPending && readiness.entitySpawnsPending) ? " + " : "",
                                 readiness.entitySpawnsPending ? "entity spawns" : "");
                    }

                    ImGui::SetTooltip("Sector (%d, %d) - %s\n"
                                      "Entities: %u | Pending: %s\n"
                                      "Terrain tiles: (%d,%d) to (%d,%d)\n"
                                      "Click to %s",
                                      x, z, stateStr,
                                      readiness.entityCount, pendingStr,
                                      x * tps, z * tps,
                                      (x + 1) * tps - 1, (z + 1) * tps - 1,
                                      clickAction);
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

        // Load once so slider edits aren't clobbered by the live query every frame
        if (!streamingConfigLoaded)
        {
            editableStreaming = dispatcher.query(events::world::GetWorldStreamingStatsQuery{});
            streamingConfigLoaded = true;
        }

        bool changed = false;

        changed |= ImGui::SliderFloat("Load Radius", &editableStreaming.loadRadius,
                                      1.0f, 32.0f, "%.1f sectors");
        changed |= ImGui::SliderFloat("Prefetch Radius", &editableStreaming.prefetchRadius,
                                      0.0f, 40.0f, "%.1f sectors");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("VK-1591: sectors between the load and prefetch radii have their\n"
                              "bytes read into memory but spawn NO entities, so crossing into the\n"
                              "load radius costs no file read.\n"
                              "0 means \"same as Load Radius\" - no prefetch ring.");
        changed |= ImGui::SliderFloat("Unload Radius", &editableStreaming.unloadRadius,
                                      2.0f, 48.0f, "%.1f sectors");
        changed |= ImGui::SliderInt("Max Loads/Frame", &editableStreaming.maxLoadsPerFrame, 1, 16);
        changed |= ImGui::SliderInt("Max Prefetches/Frame", &editableStreaming.maxPrefetchesPerFrame, 0, 16);
        changed |= ImGui::SliderInt("Max Unloads/Frame", &editableStreaming.maxUnloadsPerFrame, 1, 16);
        changed |= ImGui::SliderInt("Max Entities/Frame", &editableStreaming.maxEntitiesPerFrame, 1, 64);

        ImGui::Separator();
        ImGui::Text("Terrain Tile Streaming (via Sector)");
        changed |= ImGui::SliderInt("Max Terrain Loads/Frame",
                                    &editableStreaming.maxTerrainLoadsPerFrame, 1, 16);
        changed |= ImGui::SliderInt("Max Terrain Unloads/Frame",
                                    &editableStreaming.maxTerrainUnloadsPerFrame, 1, 16);

        ImGui::Separator();
        ImGui::Text("Predictive Streaming (VK-1593)");
        changed |= ImGui::SliderFloat("Lookahead", &editableStreaming.lookaheadSeconds,
                                      0.0f, 5.0f, "%.2f s");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Score each sector against the CLOSER of the source's current\n"
                              "position and position + velocity x lookahead, so sectors ahead\n"
                              "of motion load before equidistant ones behind it.\n"
                              "0 = off. Prediction never reaches past the outer ring.");
        changed |= ImGui::SliderFloat("View Bias", &editableStreaming.viewBiasStrength,
                                      0.0f, 4.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Push sectors outside the camera's look direction down the load\n"
                              "ORDER (never out of the ring, never into the unload pass).\n"
                              "0 = off - the right setting for a top-down camera, whose forward\n"
                              "barely projects onto the XZ plane. Play mode only: the editor\n"
                              "viewport reports no look direction.");
        changed |= ImGui::SliderFloat("Teleport Threshold", &editableStreaming.teleportThresholdSectors,
                                      0.0f, 16.0f, "%.1f sectors");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A one-frame position delta beyond this is a JUMP, not motion:\n"
                              "the frame's velocity is discarded and the burst window opens.\n"
                              "0 means the 2-sector default - NOT \"disabled\".");
        changed |= ImGui::SliderInt("Burst Frames", &editableStreaming.burstFrames, 0, 120);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Frames of relaxed budget after a detected jump, counted from and\n"
                              "including the frame it was detected. 0 = no burst.");
        changed |= ImGui::SliderInt("Burst Max Loads/Frame",
                                    &editableStreaming.maxLoadsPerFrameBurst, 0, 64);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Activation budget while the burst window is open.\n"
                              "0 = 4x Max Loads/Frame.");
        changed |= ImGui::SliderInt("Burst Max Entities/Frame",
                                    &editableStreaming.maxEntitiesPerFrameBurst, 0, 256);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Entity spawn budget while the burst window is open.\n"
                              "0 = 4x Max Entities/Frame.");

        ImGui::Separator();
        changed |= ImGui::Checkbox("Edit-Mode Streaming", &editableStreaming.editModeStreaming);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Stream sectors around the editor camera while editing.\n"
                              "Unsaved (dirty) sectors and the selected entity's sector\n"
                              "are never auto-unloaded.");

        if (changed)
        {
            events::world::SetStreamingConfigCommand cmd;
            cmd.config = editableStreaming;
            dispatcher.execute(cmd);
            // Re-read so UI reflects validation (e.g. unloadRadius forced above loadRadius)
            editableStreaming = dispatcher.query(events::world::GetWorldStreamingStatsQuery{});
        }

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f);
        if (ImGui::SmallButton("Reload"))
            streamingConfigLoaded = false;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Discard UI edits and re-read the active config");

        ImGui::Separator();
        {
            // VK-1591: the streaming overlay for prefetch residency. `bytes` is the exact sum of
            // the raw .vfsector buffers held, not an on-disk estimate.
            auto prefetch = dispatcher.query(events::world::GetSectorPrefetchStatsQuery{});
            const double megabytes = static_cast<double>(prefetch.bytes) / (1024.0 * 1024.0);
            ImGui::Text("Prefetch ring: %u resident (+%u in flight) | %.2f MB",
                        prefetch.prefetchedSectors, prefetch.prefetchingSectors, megabytes);
            if (prefetch.byteCap != 0)
            {
                const double capMegabytes = static_cast<double>(prefetch.byteCap) / (1024.0 * 1024.0);
                if (prefetch.bytes >= prefetch.byteCap)
                    ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f),
                                       "Prefetch byte cap reached (%.2f MB) - no new prefetches",
                                       capMegabytes);
                else
                    ImGui::TextDisabled("Prefetch byte cap: %.2f MB", capMegabytes);
            }
            else
            {
                ImGui::TextDisabled("Prefetch byte cap: unlimited");
            }

            // VK-1593: the only way to observe the camera-jump burst from the editor. Counts down
            // to 0 over burstFrames once a teleport is detected.
            if (prefetch.burstFramesRemaining > 0)
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
                                   "Camera-jump burst: %d frames remaining",
                                   prefetch.burstFramesRemaining);
            else
                ImGui::TextDisabled("Camera-jump burst: idle");
        }

        ImGui::Separator();
        ImGui::Text("GPU Object Streaming: %s",
                    editableStreaming.enableGPUObjectStreaming ? "Enabled" : "Disabled");

        if (editableStreaming.enableGPUObjectStreaming)
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
            ImGui::InputFloat("Prefetch Radius (sectors, 0 = same as load)", &prefetchRadius, 1.0f, 2.0f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("VK-1591: sectors between Load and Prefetch have their bytes read\n"
                                  "into memory but spawn no entities, so entering the load ring\n"
                                  "costs no file read. 0 disables the prefetch ring entirely.");
            ImGui::InputFloat("Unload Radius (sectors)", &unloadRadius, 1.0f, 2.0f);
            ImGui::Checkbox("GPU Object Streaming", &gpuObjectStreaming);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Stream GPU objects in/out based on camera distance.\nReduces GPU memory for large worlds with >65K objects.");

            ImGui::Spacing();

            // VK-1588: validate before dispatching CreateWorldCommand. sectorSize becomes
            // SectorConfig::sectorWorldSize, the divisor in
            // WorldSectorManager::worldPositionToSectorCoord - a non-positive value makes every
            // entity collapse into sector 0. The sector grid is only uniquely addressable over
            // [kMinSectorCoord, kMaxSectorCoord], so the resulting world extent is a function of
            // sectorSize and worth surfacing here.
            constexpr ImVec4 errorColor(1.0f, 0.3f, 0.3f, 1.0f);
            const bool nameValid = worldName[0] != '\0';
            const bool sectorSizeValid = std::isfinite(sectorSize) && sectorSize > 0.0f;
            const bool loadRadiusValid = std::isfinite(loadRadius) && loadRadius >= 1.0f;
            // VK-1591: 0 is the "no prefetch ring" sentinel; any other value must sit at or
            // beyond the activate ring, and hysteresis must sit beyond the OUTERMOST residency
            // ring - which is the prefetch ring whenever one is configured.
            const bool prefetchValid = std::isfinite(prefetchRadius) &&
                (prefetchRadius == 0.0f || prefetchRadius >= loadRadius);
            const bool radiiValid = std::isfinite(unloadRadius) &&
                unloadRadius > std::max(loadRadius, prefetchRadius);
            const bool canCreate = nameValid && sectorSizeValid && loadRadiusValid &&
                                   prefetchValid && radiiValid;

            if (!nameValid)
                ImGui::TextColored(errorColor, "World name is required");
            if (!sectorSizeValid)
                ImGui::TextColored(errorColor, "Sector Size must be greater than 0");
            if (!loadRadiusValid)
                ImGui::TextColored(errorColor, "Load Radius must be at least 1 sector");
            if (loadRadiusValid && !prefetchValid)
                ImGui::TextColored(errorColor,
                                   "Prefetch Radius must be 0 (disabled) or at least Load Radius");
            if (loadRadiusValid && prefetchValid && !radiiValid)
                ImGui::TextColored(errorColor,
                                   "Unload Radius must be greater than the Load and Prefetch Radii "
                                   "(streaming hysteresis)");

            if (sectorSizeValid)
            {
                ImGui::TextDisabled("Addressable world extent: +/- %.0f units (%d sectors x %.0f)",
                                    static_cast<double>(world::kMaxSectorCoord) * sectorSize,
                                    world::kMaxSectorCoord, sectorSize);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Sector coordinates are packed into a 16-bit-per-axis\n"
                                      "streaming id. Entities beyond this extent are clamped\n"
                                      "to the boundary sector.");
            }

            ImGui::Spacing();
            if (!canCreate) ImGui::BeginDisabled();
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
                    cmd.streamingConfig.prefetchRadius = prefetchRadius;
                    cmd.streamingConfig.unloadRadius = unloadRadius;
                    cmd.streamingConfig.enableGPUObjectStreaming = gpuObjectStreaming;
                    // VK-1593: new worlds opt in to predictive streaming. The struct defaults stay
                    // "off" so every .vfworld written before VK-1593 - which omits these keys -
                    // keeps streaming exactly as it did. Tune them in the Streaming tab.
                    cmd.streamingConfig.lookaheadSeconds = 1.0f;
                    cmd.streamingConfig.burstFrames = 30;
                    events::EventDispatcher::instance().execute(cmd);
                    showCreationWizard = false;
                }
            }
            if (!canCreate) ImGui::EndDisabled();
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
        prefetchedSectors = 0;
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
                // VK-1591: own bucket - counting these as "unloaded" would be a lie, their
                // bytes are resident and are what the prefetch MB figure is measuring.
                case world::SectorState::Prefetching:
                case world::SectorState::Prefetched: prefetchedSectors++; break;
                default: unloadedSectors++; break;
                }
            }
        }

        // Refresh cached HLOD status counts
        cachedHLODCount = 0;
        cachedHLODTotal = 0;
        for (const auto& info : cachedGrid)
        {
            if (!info.exists) continue;
            cachedHLODTotal++;
            events::world::hlod::IsHLODGeneratedQuery q;
            q.coord = info.coord;
            if (dispatcher.query(q)) cachedHLODCount++;
        }
    }

    void WorldSectorWindow::drawHLODConfig()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Load config once, not every frame (prevents clobbering slider edits)
        if (!hlodConfigLoaded)
        {
            auto hlodConfig = dispatcher.query(events::world::hlod::GetHLODConfigQuery{});
            hlodEnabled = hlodConfig.enabled;
            if (hlodConfig.tiers.size() > 0)
            {
                hlodTier0Radius = hlodConfig.tiers[0].displayRadius;
                hlodTier0Ratio = hlodConfig.tiers[0].simplificationRatio;
            }
            if (hlodConfig.tiers.size() > 1)
            {
                hlodTier1Radius = hlodConfig.tiers[1].displayRadius;
                hlodTier1Ratio = hlodConfig.tiers[1].simplificationRatio;
            }
            if (hlodConfig.tiers.size() > 2)
            {
                hlodTier2Radius = hlodConfig.tiers[2].displayRadius;
                hlodTier2Ratio = hlodConfig.tiers[2].simplificationRatio;
            }
            hlodConfigLoaded = true;
        }

        bool configChanged = false;

        if (ImGui::Checkbox("Enable HLOD", &hlodEnabled))
            configChanged = true;

        ImGui::Spacing();
        ImGui::Text("Tier 0 (Per-Sector)");
        if (ImGui::SliderFloat("Display Radius##T0", &hlodTier0Radius, 5.0f, 50.0f, "%.0f sectors"))
            configChanged = true;
        if (ImGui::SliderFloat("Simplification##T0", &hlodTier0Ratio, 0.01f, 0.5f, "%.2f"))
            configChanged = true;

        ImGui::Spacing();
        ImGui::Text("Tier 1 (2x2 Sectors)");
        if (ImGui::SliderFloat("Display Radius##T1", &hlodTier1Radius, 10.0f, 80.0f, "%.0f sectors"))
            configChanged = true;
        if (ImGui::SliderFloat("Simplification##T1", &hlodTier1Ratio, 0.005f, 0.2f, "%.3f"))
            configChanged = true;

        ImGui::Spacing();
        ImGui::Text("Tier 2 (4x4 Sectors)");
        if (ImGui::SliderFloat("Display Radius##T2", &hlodTier2Radius, 20.0f, 100.0f, "%.0f sectors"))
            configChanged = true;
        if (ImGui::SliderFloat("Simplification##T2", &hlodTier2Ratio, 0.001f, 0.1f, "%.3f"))
            configChanged = true;

        if (configChanged)
        {
            events::world::hlod::SetHLODConfigCommand cmd;
            cmd.config.enabled = hlodEnabled;
            cmd.config.tiers = {
                {0, 1, hlodTier0Radius, hlodTier0Ratio},
                {1, 2, hlodTier1Radius, hlodTier1Ratio},
                {2, 4, hlodTier2Radius, hlodTier2Ratio}
            };
            dispatcher.execute(cmd);
        }

        if (ImGui::Button("Reload Config"))
            hlodConfigLoaded = false;

        ImGui::Separator();
        ImGui::Text("Generation");

        if (hlodGenerating)
        {
            // Process one sector per frame to avoid blocking the UI thread
            if (!hlodPendingSectors.empty())
            {
                const auto& coord = hlodPendingSectors.back();
                events::world::hlod::GenerateHLODCommand cmd;
                cmd.coord = coord;
                cmd.tier = 0;
                dispatcher.execute(cmd);
                hlodPendingSectors.pop_back();
                hlodDoneCount++;

                hlodGenerationProgress = static_cast<float>(hlodDoneCount) /
                    static_cast<float>(std::max(hlodTotalToGenerate, 1));
                hlodGenerationStage = "Sector " + std::to_string(hlodDoneCount) +
                    "/" + std::to_string(hlodTotalToGenerate);
            }
            else
            {
                hlodGenerating = false;
                hlodGenerationProgress = 1.0f;
                hlodGenerationStage = "Complete";
                // Refresh cached status
                cachedHLODCount = 0;
                cachedHLODTotal = 0;
                for (const auto& info : cachedGrid)
                {
                    if (!info.exists) continue;
                    cachedHLODTotal++;
                    events::world::hlod::IsHLODGeneratedQuery q;
                    q.coord = info.coord;
                    if (dispatcher.query(q)) cachedHLODCount++;
                }
            }

            ImGui::ProgressBar(hlodGenerationProgress, ImVec2(-1, 0),
                               hlodGenerationStage.c_str());
        }
        else
        {
            if (ImGui::Button("Generate All HLOD (Tier 0)"))
            {
                auto allCoords = dispatcher.query(events::world::GetLoadedSectorCoordsQuery{});
                hlodPendingSectors = std::move(allCoords);
                hlodTotalToGenerate = static_cast<int>(hlodPendingSectors.size());
                hlodDoneCount = 0;
                hlodGenerationProgress = 0.0f;
                hlodGenerationStage = "Starting...";
                hlodGenerating = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Generate Missing"))
            {
                // Only sectors without a bake — covers invalidated (stale) HLODs
                auto allCoords = dispatcher.query(events::world::GetLoadedSectorCoordsQuery{});
                hlodPendingSectors.clear();
                for (const auto& coord : allCoords)
                {
                    events::world::hlod::IsHLODGeneratedQuery q;
                    q.coord = coord;
                    if (!dispatcher.query(q))
                        hlodPendingSectors.push_back(coord);
                }
                hlodTotalToGenerate = static_cast<int>(hlodPendingSectors.size());
                hlodDoneCount = 0;
                hlodGenerationProgress = 0.0f;
                hlodGenerationStage = "Starting...";
                hlodGenerating = hlodTotalToGenerate > 0;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Re-bake only sectors whose HLOD is missing or was\n"
                                  "invalidated by a content change (saved dirty sector)");
        }

        // Status: use cached counts (refreshed on timer in refreshStats, not per-frame)
        ImGui::Separator();
        ImGui::Text("Status");
        ImGui::Text("HLOD Generated: %d / %d sectors", cachedHLODCount, cachedHLODTotal);
    }

} // namespace windows
