#include "TerrainCreationWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/TerrainEvents.hpp"
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
            ImGui::SliderInt("Tiles X", &tilesX, 1, 16);
            ImGui::SliderInt("Tiles Z", &tilesZ, 1, 16);

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

            if (ImGui::TreeNode("LOD Distances"))
            {
                ImGui::DragFloat("LOD 0 Distance", &lodDistances[0], 10.0f, 10.0f, 1000.0f, "%.0f");
                ImGui::DragFloat("LOD 1 Distance", &lodDistances[1], 10.0f, 50.0f, 2000.0f, "%.0f");
                ImGui::DragFloat("LOD 2 Distance", &lodDistances[2], 10.0f, 100.0f, 3000.0f, "%.0f");
                ImGui::DragFloat("LOD 3 Distance", &lodDistances[3], 10.0f, 200.0f, 5000.0f, "%.0f");
                ImGui::TreePop();
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

            if (ImGui::Button("Create", ImVec2(buttonWidth, 0)))
            {
                createTerrain();
                visible = false;
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
        lodDistances = { 100.0f, 300.0f, 600.0f, 1200.0f };
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

        for (int i = 0; i < 4; ++i)
        {
            config.lodDistances[i] = lodDistances[i];
        }

        events::terrain::CreateTerrainCommand cmd;
        cmd.config = config;
        events::EventDispatcher::instance().execute(cmd);
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
            {L"Heightmap Files (*.vfImage;*.raw)", L"*.vfImage;*.raw"}
        };

        std::string selectedFile = fileDialog.openFileDialog(fileTypes);
        if (!selectedFile.empty())
        {
            heightmapPath = selectedFile;
        }
    }
}
