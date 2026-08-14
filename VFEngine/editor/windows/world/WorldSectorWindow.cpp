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
#include <format>
#include "nfd/FileDialog.hpp"

namespace windows
{
    const std::vector<std::pair<std::wstring, std::wstring>> WorldSectorWindow::WORLD_FILE_TYPES = {
        {L"World Files", L"*.vfworld"}
    };

    WorldSectorWindow::WorldSectorWindow()
    {
        // Covers every load path, including the scene-load auto-load that never touches this
        // window. createWorld and clearWorld do not publish this, so they reset the latches at
        // their own call sites below.
        worldLoadedToken = events::EventDispatcher::instance()
            .subscribe<events::world::WorldLoadedNotification>(
                [this](const events::world::WorldLoadedNotification&)
                {
                    invalidateConfigCaches();
                });
    }

    WorldSectorWindow::~WorldSectorWindow()
    {
        if (worldLoadedToken.isValid())
            events::EventDispatcher::instance().unsubscribe(worldLoadedToken);
    }

    void WorldSectorWindow::invalidateConfigCaches()
    {
        // VK-1599: the incoming world may declare fewer grids than the outgoing one. Falling back
        // to the primary grid is the only index guaranteed to exist; drawGridSelector clamps too,
        // but this runs before the next draw and keeps every cache below consistent with it.
        activeGrid = 0;
        cachedGrids.clear();
        gridActionMessage.clear();

        streamingConfigLoaded = false;
        overrideLoaded = false;
        hlodConfigLoaded = false;

        // VK-1596: the detached-layer stash holds bytes belonging to the OUTGOING world's sector
        // coords. Carrying it across would let a re-check paste one world's fog-of-war into
        // another's identically numbered sector.
        detachedLayers.clear();
        cachedLayers = {};
        cachedSectorLayers.clear();
        selectedLayer.clear();
        layerError.clear();
        exportSectorIndex = 0;

        // VK-1598: the dry run describes ONE world's sector set, so carrying it across would offer
        // an Apply button backed by another world's numbers. The status line survives on purpose -
        // a successful repartition reloads the world, which is what fires this.
        repartitionSeeded = false;
        repartitionPreviewValid = false;
        repartitionPreview = {};
    }
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

                // VK-1599: one selector above the tab bar rather than one per tab. Sector Grid,
                // Streaming Config and Data Layers all operate on a single grid, and having them
                // disagree about which one would be the whole bug class this feature can produce.
                drawGridSelector();

                if (ImGui::BeginTabBar("WorldSectorTabs"))
                {
                    if (ImGui::BeginTabItem("Sector Grid"))
                    {
                        drawSectorGrid();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Grids")) // VK-1599
                    {
                        drawGridManagement();
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
                    if (ImGui::BeginTabItem("Data Layers")) // VK-1596
                    {
                        drawDataLayers();
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

                // VK-1595: the toggle lives here, the panel draws in the viewport. Session-only.
                ImGui::SameLine();
                bool overlayVisible =
                    dispatcher.query(events::world::GetStreamingOverlayVisibleQuery{});
                if (ImGui::Checkbox("Streaming Overlay", &overlayVisible))
                {
                    events::world::SetStreamingOverlayVisibleCommand cmd;
                    cmd.visible = overlayVisible;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Top-down sector state panel pinned in the viewport,\n"
                                      "with source markers and load/prefetch/unload rings.\n"
                                      "Works in play-in-editor.");

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
                    invalidateConfigCaches(); // clearWorld publishes no WorldLoadedNotification
                }

                // VK-1597: an always-loaded entity is persisted by the SCENE file, and Save World
                // does not write that. Deliberately a warning rather than an implicit Save Scene:
                // saveScene is sector-unaware, so auto-saving here would bake every currently
                // resident sector entity into the .vfscene and pin them all always-loaded.
                const uint32_t pendingMigrations =
                    dispatcher.query(events::world::GetAlwaysLoadedMigrationCountQuery{});
                if (pendingMigrations > 0)
                {
                    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.2f, 1.0f),
                                       "%u entity(ies) moved to the always-loaded set - Save Scene to persist them.",
                                       pendingMigrations);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("These entities were removed from their sector because\n"
                                          "\"Spatially Loaded\" is unchecked. They now live in the\n"
                                          "scene file, so File > Save Scene is what writes them.");
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

            // VK-1598: at the window root and OUTSIDE the isWorld branch, for two reasons -
            // OpenPopup and BeginPopupModal must share a popup-stack ID scope (a tab item pushes
            // its own), and Convert to Flat closes the world, so its Save-Scene prompt has to be
            // reachable on the frame after isWorld goes false.
            drawRepartitionModals();

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

    void WorldSectorWindow::onActiveGridChanged()
    {
        // VK-1599: every cache in this window describes ONE grid. The streaming-config latch is the
        // dangerous one - VK-1595 showed that a stale latch turns the first slider drag into a
        // whole-config write onto the wrong target - but the data-layer caches matter too: their
        // coord lists drive writes that now carry a grid index, so acting on another grid's coords
        // would address a sector that may not exist.
        streamingConfigLoaded = false;
        overrideLoaded = false;

        cachedLayers = {};
        cachedSectorLayers.clear();
        selectedLayer.clear();
        layerError.clear();
        exportSectorIndex = 0;

        // The dry run describes one grid's sector set, so an Apply offered against another grid's
        // numbers would be exactly wrong.
        repartitionSeeded = false;
        repartitionPreviewValid = false;
        repartitionPreview = {};
    }

    void WorldSectorWindow::drawGridSelector()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        cachedGrids = dispatcher.query(events::world::GetWorldGridsQuery{});

        if (cachedGrids.empty())
            return; // no world open; the caller only draws this in world mode, but be defensive

        // Clamp every frame rather than only on change: Remove Grid and a world reload can both
        // shrink the list under a selection the user made a moment ago.
        if (activeGrid >= cachedGrids.size())
            activeGrid = 0;

        // A single-grid world has nothing to pick. Drawing a one-entry combo would only teach the
        // user that grids exist without giving them anything to do - the Grids tab does that.
        if (cachedGrids.size() <= 1)
            return;

        std::vector<const char*> labels;
        labels.reserve(cachedGrids.size());
        for (const auto& grid : cachedGrids)
            labels.push_back(grid.name.c_str());

        int selected = static_cast<int>(activeGrid);
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::Combo("Grid", &selected, labels.data(), static_cast<int>(labels.size())))
        {
            activeGrid = static_cast<uint8_t>(selected);
            onActiveGridChanged();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Which streaming grid the Sector Grid, Streaming Config and\n"
                              "Data Layers tabs below operate on.");
        }

        ImGui::SameLine();
        const auto& active = cachedGrids[activeGrid];
        ImGui::TextDisabled("%.0f units/sector | %u sectors (%u loaded)%s",
                            active.sectorConfig.sectorWorldSize, active.sectorCount,
                            active.loadedSectorCount,
                            active.drivesWorldSystems ? " | drives terrain/navmesh/HLOD" : "");
    }

    void WorldSectorWindow::drawGridManagement()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::TextWrapped(
            "A world streams through one or more named grids. Each has its own cell size and load "
            "radius, so short-range clutter and long-range landmarks can stream independently.");
        ImGui::Spacing();
        ImGui::TextDisabled(
            "The Default grid is the only one that drives terrain, water, navmesh and HLOD.");
        ImGui::Separator();

        if (ImGui::BeginTable("GridsTable", 5,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Cell Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Sectors", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Drives World", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();

            // Deferred past the loop: removeGrid mutates the list cachedGrids mirrors, and acting
            // inside the row would invalidate the reference being iterated. Same reason VK-1596's
            // Data Layers tab defers applyLayerPresence past EndTable.
            int pendingRemove = -1;

            for (const auto& grid : cachedGrids)
            {
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(grid.gridIndex));

                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(grid.name.c_str(), grid.gridIndex == activeGrid,
                                      ImGuiSelectableFlags_SpanAllColumns))
                {
                    activeGrid = grid.gridIndex;
                    onActiveGridChanged();
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.0f", grid.sectorConfig.sectorWorldSize);

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u (%u)", grid.sectorCount, grid.loadedSectorCount);

                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(grid.drivesWorldSystems ? "yes" : "-");

                ImGui::TableSetColumnIndex(4);
                ImGui::BeginDisabled(grid.drivesWorldSystems);
                if (ImGui::SmallButton("Remove"))
                    pendingRemove = static_cast<int>(grid.gridIndex);
                ImGui::EndDisabled();
                if (grid.drivesWorldSystems &&
                    ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::SetTooltip("The Default grid cannot be removed - terrain, water,\n"
                                      "navmesh and HLOD are bound to it.");
                }

                ImGui::PopID();
            }

            ImGui::EndTable();

            if (pendingRemove >= 0)
            {
                events::world::RemoveWorldGridCommand cmd;
                cmd.gridIndex = static_cast<uint8_t>(pendingRemove);
                gridActionMessage = dispatcher.execute(cmd);
                if (gridActionMessage.empty())
                {
                    gridActionMessage = "Grid removed.";
                    activeGrid = 0;
                    onActiveGridChanged();
                }
            }
        }

        ImGui::Separator();
        ImGui::SeparatorText("Add Grid");

        ImGui::InputText("Name##newgrid", newGridName, sizeof(newGridName));
        ImGui::DragFloat("Cell Size##newgrid", &newGridSectorConfig.sectorWorldSize,
                         1.0f, 8.0f, 8192.0f, "%.0f units");
        ImGui::DragFloat("Load Radius##newgrid", &newGridStreamingConfig.loadRadius,
                         0.1f, 1.0f, 32.0f, "%.1f sectors");
        ImGui::DragFloat("Unload Radius##newgrid", &newGridStreamingConfig.unloadRadius,
                         0.1f, 2.0f, 48.0f, "%.1f sectors");

        const bool atCap = cachedGrids.size() >= world::kMaxGrids;
        // The same coherence rule normalizeStreamingConfig enforces, checked up front so the button
        // explains itself rather than silently correcting the value after the fact.
        const bool radiiOk = newGridStreamingConfig.unloadRadius > newGridStreamingConfig.loadRadius;
        const bool sizeOk = newGridSectorConfig.sectorWorldSize > 0.0f;

        ImGui::BeginDisabled(atCap || !radiiOk || !sizeOk);
        if (ImGui::Button("Add Grid"))
        {
            events::world::AddWorldGridCommand cmd;
            cmd.name = newGridName;
            cmd.sectorConfig = newGridSectorConfig;
            cmd.streamingConfig = newGridStreamingConfig;

            const uint8_t added = dispatcher.execute(cmd);
            if (added < world::kMaxGrids)
            {
                gridActionMessage = "Added grid '" + std::string(newGridName) + "'.";
                activeGrid = added;
                onActiveGridChanged();
            }
            else
            {
                gridActionMessage = "Could not add the grid (at the cap, or no world open).";
            }
        }
        ImGui::EndDisabled();

        if (atCap)
            ImGui::TextDisabled("At the %d-grid cap.", static_cast<int>(world::kMaxGrids));
        else if (!sizeOk)
            ImGui::TextDisabled("Cell size must be greater than 0.");
        else if (!radiiOk)
            ImGui::TextDisabled("Unload radius must exceed the load radius.");

        if (!gridActionMessage.empty())
        {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", gridActionMessage.c_str());
        }

        ImGui::Spacing();
        ImGui::TextDisabled(
            "Assign entities to a grid from the Details panel (Transform > Streaming Grid).\n"
            "A grid must be empty before it can be removed - its .vfsector files are the only\n"
            "copy of those entities.");
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

        // VK-1596: a highlight the user set on another tab must never be a mystery here.
        if (!selectedLayer.empty())
        {
            const world::DataLayerSummary* summary = findCachedLayer(selectedLayer);
            const uint32_t carrying = summary ? summary->sectorCount : 0;
            ImGui::TextDisabled("Amber outline: sectors carrying \"%s\" (%u of %zu loaded)",
                                selectedLayer.c_str(), carrying, cachedLayers.loadedSectors.size());
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear##layerHighlight"))
                selectedLayer.clear();
        }
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
                        cmd.gridIndex = activeGrid;
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
                        cmd.gridIndex = activeGrid;
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
                    readinessQuery.gridIndex = activeGrid;
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

                    // VK-1596: SetTooltip takes one format string, which cannot carry a
                    // variable-length layer list - hence the explicit Begin/End pair.
                    ImGui::BeginTooltip();
                    ImGui::Text("Sector (%d, %d) - %s\n"
                                "Entities: %u | Pending: %s\n"
                                "Terrain tiles: (%d,%d) to (%d,%d)\n"
                                "Click to %s",
                                x, z, stateStr,
                                readiness.entityCount, pendingStr,
                                x * tps, z * tps,
                                (x + 1) * tps - 1, (z + 1) * tps - 1,
                                clickAction);

                    // Read from the inverted summary cache - no extra query per hover.
                    if (const auto layerIt = cachedSectorLayers.find(info.coord);
                        layerIt != cachedSectorLayers.end() && !layerIt->second.empty())
                    {
                        std::string layerList;
                        for (const auto& layerName : layerIt->second)
                        {
                            if (!layerList.empty()) layerList += ", ";
                            layerList += layerName;
                        }
                        ImGui::TextDisabled("Layers: %s", layerList.c_str());
                    }
                    else if (info.state == world::SectorState::Loaded)
                    {
                        ImGui::TextDisabled("Layers: none");
                    }
                    ImGui::EndTooltip();
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

                // VK-1596: amber outline for sectors carrying the layer selected in the Data
                // Layers tab. Inset so it coexists with the camera outline drawn at exact bounds,
                // and an OUTLINE rather than a fill on purpose - the fill table above is mirrored
                // by ViewPortStreamingOverlay::cellColor so the two views agree on what a colour
                // means, and recolouring here would desync them.
                if (!selectedLayer.empty())
                {
                    const auto layerIt = cachedSectorLayers.find(info.coord);
                    if (layerIt != cachedSectorLayers.end() &&
                        std::find(layerIt->second.begin(), layerIt->second.end(), selectedLayer) !=
                            layerIt->second.end())
                    {
                        ImVec2 btnMin = ImGui::GetItemRectMin();
                        ImVec2 btnMax = ImGui::GetItemRectMax();
                        btnMin.x += 3.0f; btnMin.y += 3.0f;
                        btnMax.x -= 3.0f; btnMax.y -= 3.0f;
                        ImGui::GetWindowDrawList()->AddRect(
                            btnMin, btnMax,
                            IM_COL32(255, 190, 60, 255), 0.0f, 0, 2.0f);
                    }
                }

                ImGui::PopStyleColor();
            }

            ImGui::PopID();

            if ((i + 1) % gridWidth != 0)
                ImGui::SameLine();
        }
    }

    bool WorldSectorWindow::drawStreamingSliders(world::SectorStreamingConfig& config,
                                                 bool sessionOverride)
    {
        bool changed = false;

        changed |= ImGui::SliderFloat("Load Radius", &config.loadRadius,
                                      1.0f, 32.0f, "%.1f sectors");
        changed |= ImGui::SliderFloat("Prefetch Radius", &config.prefetchRadius,
                                      0.0f, 40.0f, "%.1f sectors");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("VK-1591: sectors between the load and prefetch radii have their\n"
                              "bytes read into memory but spawn NO entities, so crossing into the\n"
                              "load radius costs no file read.\n"
                              "0 means \"same as Load Radius\" - no prefetch ring.");
        changed |= ImGui::SliderFloat("Unload Radius", &config.unloadRadius,
                                      2.0f, 48.0f, "%.1f sectors");
        changed |= ImGui::SliderInt("Max Loads/Frame", &config.maxLoadsPerFrame, 1, 16);
        changed |= ImGui::SliderInt("Max Prefetches/Frame", &config.maxPrefetchesPerFrame, 0, 16);
        changed |= ImGui::SliderInt("Max Unloads/Frame", &config.maxUnloadsPerFrame, 1, 16);
        changed |= ImGui::SliderInt("Max Entities/Frame", &config.maxEntitiesPerFrame, 1, 64);

        ImGui::Separator();
        ImGui::Text("Terrain Tile Streaming (via Sector)");
        // VK-1595: TerrainService caches these two once, in activateTilesForLoadedSectors(), and
        // the session override is cleared on world load - so an overridden value can never reach
        // it. Disabled rather than silently inert under a heading that promises live effect.
        ImGui::BeginDisabled(sessionOverride);
        changed |= ImGui::SliderInt("Max Terrain Loads/Frame",
                                    &config.maxTerrainLoadsPerFrame, 1, 16);
        changed |= ImGui::SliderInt("Max Terrain Unloads/Frame",
                                    &config.maxTerrainUnloadsPerFrame, 1, 16);
        ImGui::EndDisabled();
        if (sessionOverride && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Not overridable: TerrainService caches these at world activation,\n"
                              "and the override never survives a world load. Use the saved config.");

        ImGui::Separator();
        ImGui::Text("Predictive Streaming (VK-1593)");
        changed |= ImGui::SliderFloat("Lookahead", &config.lookaheadSeconds,
                                      0.0f, 5.0f, "%.2f s");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Score each sector against the CLOSER of the source's current\n"
                              "position and position + velocity x lookahead, so sectors ahead\n"
                              "of motion load before equidistant ones behind it.\n"
                              "0 = off. Prediction never reaches past the outer ring.");
        changed |= ImGui::SliderFloat("View Bias", &config.viewBiasStrength,
                                      0.0f, 4.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Push sectors outside the camera's look direction down the load\n"
                              "ORDER (never out of the ring, never into the unload pass).\n"
                              "0 = off - the right setting for a top-down camera, whose forward\n"
                              "barely projects onto the XZ plane. Play mode only: the editor\n"
                              "viewport reports no look direction.");
        changed |= ImGui::SliderFloat("Teleport Threshold", &config.teleportThresholdSectors,
                                      0.0f, 16.0f, "%.1f sectors");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A one-frame position delta beyond this is a JUMP, not motion:\n"
                              "the frame's velocity is discarded and the burst window opens.\n"
                              "0 means the 2-sector default - NOT \"disabled\".");
        changed |= ImGui::SliderInt("Burst Frames", &config.burstFrames, 0, 120);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Frames of relaxed budget after a detected jump, counted from and\n"
                              "including the frame it was detected. 0 = no burst.");
        changed |= ImGui::SliderInt("Burst Max Loads/Frame",
                                    &config.maxLoadsPerFrameBurst, 0, 64);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Activation budget while the burst window is open.\n"
                              "0 = 4x Max Loads/Frame.");
        changed |= ImGui::SliderInt("Burst Max Entities/Frame",
                                    &config.maxEntitiesPerFrameBurst, 0, 256);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Entity spawn budget while the burst window is open.\n"
                              "0 = 4x Max Entities/Frame.");

        ImGui::Separator();
        changed |= ImGui::Checkbox("Edit-Mode Streaming", &config.editModeStreaming);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Stream sectors around the editor camera while editing.\n"
                              "Unsaved (dirty) sectors and the selected entity's sector\n"
                              "are never auto-unloaded.");

        return changed;
    }

    void WorldSectorWindow::drawStreamingConfig()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // VK-1598: first, because the sector size governs everything the radii below are measured
        // in - and unlike them it was unchangeable until now.
        drawRepartitionSection();
        ImGui::SeparatorText("Streaming");

        // Load once so slider edits aren't clobbered by the live query every frame.
        // VK-1595: the PERSISTED config, not the effective one — see the header note.
        // VK-1599: the latch is per (world, grid) - drawGridSelector clears it on a grid switch,
        // and the grid stamp below catches any other path that changes activeGrid.
        if (!streamingConfigLoaded || streamingConfigGrid != activeGrid)
        {
            events::world::GetPersistedStreamingConfigQuery q;
            q.gridIndex = activeGrid;
            editableStreaming = dispatcher.query(q);
            streamingConfigLoaded = true;
            streamingConfigGrid = activeGrid;
        }

        ImGui::PushID("persist");
        const bool changed = drawStreamingSliders(editableStreaming, false);
        ImGui::PopID();

        if (changed)
        {
            events::world::SetStreamingConfigCommand cmd;
            cmd.gridIndex = activeGrid;
            cmd.config = editableStreaming;
            dispatcher.execute(cmd);
            // Re-read so UI reflects validation (e.g. unloadRadius forced above loadRadius)
            events::world::GetPersistedStreamingConfigQuery reread;
            reread.gridIndex = activeGrid;
            editableStreaming = dispatcher.query(reread);
        }

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f);
        if (ImGui::SmallButton("Reload"))
            streamingConfigLoaded = false;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Discard UI edits and re-read the world's saved config");

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

        // VK-1595: last, so the persisted config and its read-outs stay together at the top and the
        // session-only controls read as the separate thing they are.
        drawStreamingDebugSection();
    }

    // VK-1595: the wp.Runtime.* corner of the tab — a config that governs the live streamers but is
    // never written to the .vfworld, plus freeze / single-step.
    void WorldSectorWindow::drawStreamingDebugSection()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::SeparatorText("Debug - session override (not saved)");

        events::world::GetStreamingConfigOverrideQuery overrideQuery;
        overrideQuery.gridIndex = activeGrid;
        auto activeOverride = dispatcher.query(overrideQuery);
        bool overrideEnabled = activeOverride.has_value();

        if (ImGui::Checkbox("Enable session override", &overrideEnabled))
        {
            if (overrideEnabled)
            {
                // Seed from what the streamer is running right now, so switching the override on
                // changes nothing until a slider is actually moved.
                events::world::GetWorldStreamingStatsQuery statsQuery;
                statsQuery.gridIndex = activeGrid;
                overrideStreaming = dispatcher.query(statsQuery);
                overrideLoaded = true;

                events::world::SetStreamingConfigOverrideCommand cmd;
                cmd.gridIndex = activeGrid;
                cmd.config = overrideStreaming;
                dispatcher.execute(cmd);
            }
            else
            {
                events::world::ClearStreamingConfigOverrideCommand clearCmd;
                clearCmd.gridIndex = activeGrid;
                dispatcher.execute(clearCmd);
                overrideLoaded = false;
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Tune radii and budgets against the LIVE streamer without touching\n"
                              "the world. Nothing here reaches the .vfworld - Save World still\n"
                              "writes the values in the section above.\n"
                              "Kept across play/stop; cleared when the world closes.");

        if (activeOverride.has_value())
        {
            // Adopt the service's normalized copy the first frame the override exists, so a value
            // the service clamped (e.g. unloadRadius pushed past the prefetch ring) is what the
            // sliders show.
            if (!overrideLoaded)
            {
                overrideStreaming = *activeOverride;
                overrideLoaded = true;
            }

            ImGui::PushID("override");
            const bool overrideChanged = drawStreamingSliders(overrideStreaming, true);
            ImGui::PopID();

            if (overrideChanged)
            {
                events::world::SetStreamingConfigOverrideCommand cmd;
                cmd.gridIndex = activeGrid;
                cmd.config = overrideStreaming;
                dispatcher.execute(cmd);
                overrideStreaming =
                    dispatcher.query([this] {
                        events::world::GetWorldStreamingStatsQuery q;
                        q.gridIndex = activeGrid;
                        return q;
                    }());
            }

            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                               "Override active - the streamer is running these values.");
        }
        else
        {
            overrideLoaded = false;
        }

        ImGui::Spacing();

        const bool paused = dispatcher.query(events::world::GetStreamingPausedQuery{});
        if (ImGui::Button(paused ? "Resume Streaming" : "Pause Streaming"))
        {
            events::world::SetStreamingPausedCommand cmd;
            cmd.paused = !paused;
            dispatcher.execute(cmd);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Freeze the streamer's DECISIONS - no new sector loads, prefetches or\n"
                              "unloads, and no new HLOD proxy load/unload. Work already in flight\n"
                              "still completes: reads land, queued entities spawn, and in-progress\n"
                              "HLOD crossfades finish (a proxy mid fade-out is still reaped), so a\n"
                              "paused frame settles rather than stalling half-loaded.\n"
                              "Resuming clears the motion history, so a pause never fakes a jump.");

        ImGui::SameLine();
        ImGui::BeginDisabled(!paused);
        if (ImGui::Button("Step 1 Frame"))
            dispatcher.execute(events::world::StepStreamingFrameCommand{});
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Let exactly one streaming decision pass through, then freeze again");

        if (paused)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.25f, 1.0f), "PAUSED");
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
                    invalidateConfigCaches(); // createWorld publishes no WorldLoadedNotification
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

        events::world::GetSectorConfigQuery sectorConfigQuery;
        sectorConfigQuery.gridIndex = activeGrid;
        auto sectorConfig = dispatcher.query(sectorConfigQuery);
        cachedTilesPerSector = sectorConfig.tilesPerSector;

        // Compute camera sector from editor camera position
        {
            events::world::GetSectorAtPositionQuery posQuery;
            posQuery.gridIndex = activeGrid;
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
                existQuery.gridIndex = activeGrid;
                existQuery.coord = info.coord;
                info.exists = dispatcher.query(existQuery);

                if (info.exists)
                {
                    events::world::GetSectorStateQuery stateQuery;
                    stateQuery.gridIndex = activeGrid;
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

        refreshDataLayers(); // VK-1596 - self-gating, see its definition

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

        const auto bake = dispatcher.query(events::world::hlod::GetHLODBakeProgressQuery{});

        if (bake.running)
        {
            ImGui::ProgressBar(bake.fraction(), ImVec2(-1, 0),
                               std::format("Tier {} — {}/{} cells", bake.currentTier,
                                           bake.cellsDone, bake.cellsTotal).c_str());

            if (ImGui::Button("Cancel Bake"))
                dispatcher.execute(events::world::hlod::CancelHLODBakeCommand{});
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Cells already in flight finish, then the bake stops.\n"
                                  "No partially written .vfHLOD is ever published.");

            ImGui::TextDisabled("Saving the world is blocked while a bake runs.");
        }
        else
        {
            if (ImGui::Button("Generate All HLOD"))
            {
                // VK-1594: bakes every configured tier, on the JobSystem. The previous button
                // seeded itself from GetLoadedSectorCoordsQuery, which silently skipped every
                // sector that was not currently streamed in.
                events::world::hlod::GenerateAllHLODCommand cmd;
                cmd.missingOnly = false;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Re-bake every tier for every saved sector in the world");

            ImGui::SameLine();
            if (ImGui::Button("Generate Missing"))
            {
                events::world::hlod::GenerateAllHLODCommand cmd;
                cmd.missingOnly = true;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Bake only cells whose HLOD is missing or was\n"
                                  "invalidated by a content change (saved dirty sector)");
        }

        // Refresh the cached status once, on the frame the bake finishes
        if (hlodBakeWasRunning && !bake.running)
        {
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
        hlodBakeWasRunning = bake.running;

        // Status: use cached counts (refreshed on timer in refreshStats, not per-frame)
        ImGui::Separator();
        ImGui::Text("Status");
        ImGui::Text("HLOD Generated: %d / %d sectors", cachedHLODCount, cachedHLODTotal);
        if (!bake.running && bake.cellsTotal > 0)
        {
            ImGui::Text("Last bake: %u cells, %u failed%s", bake.cellsDone, bake.cellsFailed,
                        bake.cancelled ? " (cancelled)" : "");
        }
    }

} // namespace windows
