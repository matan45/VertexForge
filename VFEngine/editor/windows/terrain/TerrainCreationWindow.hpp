#pragma once
#include "nfd/FileDialog.hpp"
#include <string>
#include <array>

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

        bool creationInProgress = false;
        float creationProgress = 0.0f;
        std::string creationStage;

    public:
        void draw();

        void show();

    private:
        void resetDefaults();
        void createTerrain();
        void browseHeightmap();
        void loadTerrain();
        void pollTerrainCreation();
    };
}
