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
        config.heightmapPath = heightmapPath;

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
