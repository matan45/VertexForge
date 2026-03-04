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
        std::array<float, 4> lodDistances = { 100.0f, 300.0f, 600.0f, 1200.0f };

    public:
        void draw();

        void show();

    private:
        void resetDefaults();
        void createTerrain();
        void browseHeightmap();
        void loadTerrain();
    };
}
