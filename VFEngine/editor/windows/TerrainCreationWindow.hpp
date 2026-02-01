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

        // Configuration state
        int tilesX = 4;
        int tilesZ = 4;
        int resolutionIndex = 0;  // 0=Low, 1=Medium, 2=High, 3=Ultra
        float worldTileSize = 32.0f;
        float maxHeight = 100.0f;
        float minHeight = -10.0f;
        std::string heightmapPath;

        // LOD distances
        std::array<float, 4> lodDistances = { 100.0f, 300.0f, 600.0f, 1200.0f };

    public:
        void draw();

        void show();
        void hide() { visible = false; }
        bool isVisible() const { return visible; }

    private:
        void resetDefaults();
        void createTerrain();
        void browseHeightmap();
    };
}
