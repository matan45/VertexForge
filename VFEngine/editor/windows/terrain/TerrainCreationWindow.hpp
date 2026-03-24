#pragma once
#include "nfd/FileDialog.hpp"
#include "data/EntityHandle.hpp"
#include "data/TerrainData.hpp"
#include <string>
#include <vector>
#include <array>
#include <future>

namespace windows
{
    class TerrainCreationWindow
    {
    private:
        bool visible = false;
        nfd::FileDialog fileDialog;

        int tilesX = 4;
        int tilesZ = 4;
        int resolutionIndex = 0;
        float worldTileSize = 32.0f;
        float maxHeight = 100.0f;
        float minHeight = -10.0f;
        std::string heightmapPath;

        bool useTiledHeightmaps = false;
        std::vector<services::HeightmapRegionData> heightmapRegions;

        bool creationInProgress = false;
        float creationProgress = 0.0f;
        std::string creationStage;

        bool pendingSaveDialog = false;
        bool saveInProgress = false;
        services::EntityHandle createdTerrainEntity;
        std::future<bool> pendingSave;

    public:
        void draw();

        void show();

    private:
        void resetDefaults();
        void createTerrain();
        void browseHeightmap();
        void browseRegionHeightmap(size_t regionIndex);
        void drawTiledHeightmapUI();
        void loadTerrain();
        void pollTerrainCreation();
        void promptSaveAfterCreation();
        void pollSaveResult();
    };
}
