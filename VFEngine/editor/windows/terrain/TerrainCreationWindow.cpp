#include "TerrainCreationWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "threading/JobSystem.hpp"
#include "print/Log.hpp"
#include <imgui.h>

namespace windows
{
    void TerrainCreationWindow::draw()
    {
        if (!visible)
            return;

        ImGui::SetNextWindowSize(ImVec2(450, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Create Terrain", &visible))
        {
            ImGui::Text("Terrain Configuration");
            ImGui::Separator();

            ImGui::Text("Grid Size (Tiles):");
            ImGui::SliderInt("Tiles X", &tilesX, 1, 100);
            ImGui::SliderInt("Tiles Z", &tilesZ, 1, 100);

            int totalTiles = tilesX * tilesZ;
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Total tiles: %d", totalTiles);

            ImGui::Separator();

            const char* resolutionNames[] = { "Low (33x33)", "Medium (65x65)", "High (129x129)" };
            ImGui::Combo("Tile Resolution", &resolutionIndex, resolutionNames, 3);

            ImGui::DragFloat("World Tile Size", &worldTileSize, 1.0f, 8.0f, 256.0f, "%.1f");

            float totalWorldSizeX = static_cast<float>(tilesX) * worldTileSize;
            float totalWorldSizeZ = static_cast<float>(tilesZ) * worldTileSize;
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Total terrain size: %.1f x %.1f units",
                              totalWorldSizeX, totalWorldSizeZ);

            ImGui::Separator();

            ImGui::Text("Height Range:");
            ImGui::DragFloat("Max Height", &maxHeight, 1.0f, 0.0f, 500.0f, "%.1f");
            ImGui::DragFloat("Min Height", &minHeight, 1.0f, -100.0f, 0.0f, "%.1f");

            if (minHeight >= maxHeight)
            {
                minHeight = maxHeight - 1.0f;
            }

            ImGui::Separator();

            ImGui::Text("Heightmap (optional):");
            ImGui::Checkbox("Use Tiled Heightmaps", &useTiledHeightmaps);

            if (useTiledHeightmaps)
            {
                drawTiledHeightmapUI();
            }
            else
            {
                if (ImGui::Button("Browse..."))
                {
                    browseHeightmap();
                }
                ImGui::SameLine();

                if (heightmapPath.empty())
                {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No heightmap (flat terrain)");
                }
                else
                {
                    size_t lastSlash = heightmapPath.find_last_of("/\\");
                    std::string filename = (lastSlash != std::string::npos)
                        ? heightmapPath.substr(lastSlash + 1)
                        : heightmapPath;
                    ImGui::Text("%s", filename.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear"))
                    {
                        heightmapPath.clear();
                    }
                }
            }

            ImGui::Separator();
            ImGui::Spacing();

            float buttonWidth = 120.0f;

            if (creationInProgress)
            {
                pollTerrainCreation();
                ImGui::ProgressBar(creationProgress, ImVec2(-1, 0), creationStage.c_str());
            }
            else if (pendingSaveDialog)
            {
                promptSaveAfterCreation();
            }
            else if (saveInProgress)
            {
                pollSaveResult();
                ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()),
                                   ImVec2(-1, 0), "Saving terrain...");
            }
            else
            {
                if (ImGui::Button("Create", ImVec2(buttonWidth, 0)))
                {
                    createTerrain();
                }

                ImGui::SameLine();

                if (ImGui::Button("Load Terrain...", ImVec2(buttonWidth, 0)))
                {
                    loadTerrain();
                    visible = false;
                }

                ImGui::SameLine();

                if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0)))
                {
                    visible = false;
                }
            }
        }
        ImGui::End();
    }

    void TerrainCreationWindow::show()
    {
        visible = true;
        resetDefaults();
    }

    void TerrainCreationWindow::resetDefaults()
    {
        tilesX = 4;
        tilesZ = 4;
        resolutionIndex = 0;
        worldTileSize = 32.0f;
        maxHeight = 100.0f;
        minHeight = -10.0f;
        heightmapPath.clear();
        useTiledHeightmaps = false;
        heightmapRegions.clear();
    }

    void TerrainCreationWindow::createTerrain()
    {
        services::TerrainCreationData config;
        config.tilesX = tilesX;
        config.tilesZ = tilesZ;
        config.resolution = static_cast<uint8_t>(resolutionIndex);
        config.worldTileSize = worldTileSize;
        config.maxHeight = maxHeight;
        config.minHeight = minHeight;

        if (useTiledHeightmaps)
        {
            config.heightmapRegions = heightmapRegions;
        }
        else
        {
            config.heightmapPath = heightmapPath;
        }

        events::terrain::BeginCreateTerrainCommand cmd;
        cmd.config = config;
        bool started = events::EventDispatcher::instance().execute(cmd);
        if (started)
        {
            creationInProgress = true;
            creationProgress = 0.0f;
            creationStage = "Starting...";
        }
    }

    void TerrainCreationWindow::pollTerrainCreation()
    {
        events::terrain::PollCreateTerrainCommand pollCmd;
        auto result = events::EventDispatcher::instance().execute(pollCmd);

        creationProgress = result.progress;
        creationStage = result.stage;

        if (!result.inProgress)
        {
            creationInProgress = false;
            if (result.result.has_value())
            {
                createdTerrainEntity = result.result.value();
                pendingSaveDialog = true;
            }
            else
            {
                visible = false;
            }
        }
    }

    void TerrainCreationWindow::drawTiledHeightmapUI()
    {
        int32_t halfX = tilesX / 2;
        int32_t halfZ = tilesZ / 2;
        int32_t gridMinX = -halfX;
        int32_t gridMinZ = -halfZ;
        int32_t gridMaxX = tilesX - halfX - 1;
        int32_t gridMaxZ = tilesZ - halfZ - 1;

        if (ImGui::Button("Add Region"))
        {
            services::HeightmapRegionData region;
            region.tileMinX = gridMinX;
            region.tileMinZ = gridMinZ;
            region.tileMaxX = gridMaxX;
            region.tileMaxZ = gridMaxZ;
            heightmapRegions.push_back(region);
        }

        int coveredTiles = 0;
        int totalTiles = tilesX * tilesZ;

        ImGui::BeginChild("RegionList", ImVec2(0, 200), true);
        int removeIndex = -1;
        for (size_t i = 0; i < heightmapRegions.size(); ++i)
        {
            auto& region = heightmapRegions[i];
            ImGui::PushID(static_cast<int>(i));

            ImGui::Text("Region %zu", i + 1);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
            if (ImGui::SmallButton("X"))
            {
                removeIndex = static_cast<int>(i);
            }

            std::string browseLabel = "Browse##" + std::to_string(i);
            if (ImGui::Button(browseLabel.c_str()))
            {
                browseRegionHeightmap(i);
            }
            ImGui::SameLine();
            if (region.filePath.empty())
            {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No file selected");
            }
            else
            {
                size_t lastSlash = region.filePath.find_last_of("/\\");
                std::string filename = (lastSlash != std::string::npos)
                    ? region.filePath.substr(lastSlash + 1)
                    : region.filePath;
                ImGui::Text("%s", filename.c_str());
            }

            ImGui::Text("Tile Range:");
            ImGui::PushItemWidth(80.0f);
            ImGui::InputInt("MinX", &region.tileMinX, 0, 0);
            ImGui::SameLine();
            ImGui::InputInt("MinZ", &region.tileMinZ, 0, 0);
            ImGui::InputInt("MaxX", &region.tileMaxX, 0, 0);
            ImGui::SameLine();
            ImGui::InputInt("MaxZ", &region.tileMaxZ, 0, 0);
            ImGui::PopItemWidth();

            // Clamp to grid bounds
            region.tileMinX = std::max(region.tileMinX, gridMinX);
            region.tileMinZ = std::max(region.tileMinZ, gridMinZ);
            region.tileMaxX = std::min(region.tileMaxX, gridMaxX);
            region.tileMaxZ = std::min(region.tileMaxZ, gridMaxZ);
            if (region.tileMinX > region.tileMaxX) region.tileMinX = region.tileMaxX;
            if (region.tileMinZ > region.tileMaxZ) region.tileMinZ = region.tileMaxZ;

            int regionTiles = (region.tileMaxX - region.tileMinX + 1) * (region.tileMaxZ - region.tileMinZ + 1);
            coveredTiles += regionTiles;

            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndChild();

        if (removeIndex >= 0)
        {
            heightmapRegions.erase(heightmapRegions.begin() + removeIndex);
        }

        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "%zu region(s), ~%d/%d tiles covered", heightmapRegions.size(), coveredTiles, totalTiles);
    }

    void TerrainCreationWindow::browseRegionHeightmap(size_t regionIndex)
    {
        if (regionIndex >= heightmapRegions.size())
            return;

        std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
            {L"Heightmap Files (*.vfImage;*.vfSVT)", L"*.vfImage;*.vfSVT"}
        };

        std::string selectedFile = fileDialog.openFileDialog(fileTypes);
        if (!selectedFile.empty())
        {
            heightmapRegions[regionIndex].filePath = selectedFile;
        }
    }

    void TerrainCreationWindow::loadTerrain()
    {
        std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
            {L"VF Terrain (*.vfTerrain)", L"*.vfTerrain"}
        };

        std::string path = fileDialog.openFileDialog(fileTypes);
        if (!path.empty())
        {
            events::terrain::BeginTerrainLoadCommand cmd;
            cmd.path = path;
            events::EventDispatcher::instance().execute(cmd);
        }
    }

    void TerrainCreationWindow::browseHeightmap()
    {
        std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
            {L"Heightmap Files (*.vfImage;*.vfSVT)", L"*.vfImage;*.vfSVT"}
        };

        std::string selectedFile = fileDialog.openFileDialog(fileTypes);
        if (!selectedFile.empty())
        {
            heightmapPath = selectedFile;
        }
    }

    void TerrainCreationWindow::promptSaveAfterCreation()
    {
        pendingSaveDialog = false;

        std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
            {L"VF Terrain (*.vfTerrain)", L"*.vfTerrain"}
        };

        std::string path = fileDialog.saveFileDialog(fileTypes, L"vfTerrain");
        if (path.empty())
        {
            vfLogWarning("Terrain created without saving. Streaming unavailable until saved.");
            visible = false;
            return;
        }

        saveInProgress = true;
        auto& dispatcher = events::EventDispatcher::instance();

        events::terrain::SetTerrainSaveLockCommand lockCmd;
        lockCmd.locked = true;
        dispatcher.execute(lockCmd);

        events::terrain::PrepareTerrainSaveCommand prepCmd;
        prepCmd.terrainEntity = createdTerrainEntity;
        prepCmd.incremental = false;
        dispatcher.execute(prepCmd);

        auto handle = createdTerrainEntity;
        pendingSave = threading::JobSystem::instance().submit(
            [handle, path]()
            {
                events::terrain::SaveTerrainCommand cmd;
                cmd.terrainEntity = handle;
                cmd.path = path;
                cmd.incremental = false;
                return events::EventDispatcher::instance().execute(cmd);
            },
            threading::JobPriority::LOW);
    }

    void TerrainCreationWindow::pollSaveResult()
    {
        if (!pendingSave.valid()) return;

        if (pendingSave.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        {
            bool success = pendingSave.get();
            saveInProgress = false;

            auto& dispatcher = events::EventDispatcher::instance();

            events::terrain::SetTerrainSaveLockCommand lockCmd;
            lockCmd.locked = false;
            dispatcher.execute(lockCmd);

            if (success)
            {
                events::terrain::SetTerrainStreamingEnabledCommand streamCmd;
                streamCmd.terrainEntity = createdTerrainEntity;
                streamCmd.enabled = true;
                dispatcher.execute(streamCmd);

                vfLogInfo("Terrain saved and streaming enabled from file.");
            }
            else
            {
                vfLogError("Failed to save terrain after creation.");
            }

            visible = false;
        }
    }
}
