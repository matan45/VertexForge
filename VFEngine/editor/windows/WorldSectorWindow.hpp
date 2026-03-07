#pragma once

#include "imguiHandler/ImguiWindow.hpp"
#include "world/WorldTypes.hpp"
#include <string>

namespace windows
{
    class WorldSectorWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        bool visible = false;
        float refreshTimer = 0.0f;
        static constexpr float REFRESH_INTERVAL = 0.25f;

        // Creation wizard state
        bool showCreationWizard = false;
        char worldName[128] = "New World";
        char worldPath[512] = "";
        float sectorSize = 128.0f;
        int tilesPerSector = 4;
        float loadRadius = 512.0f;
        float unloadRadius = 640.0f;

        // Cached stats
        int totalSectors = 0;
        int loadedSectors = 0;
        int unloadedSectors = 0;
        int loadingSectors = 0;

    public:
        explicit WorldSectorWindow() = default;
        ~WorldSectorWindow() override = default;

        void draw() override;
        void show() { visible = true; }

    private:
        void drawWorldInfo();
        void drawSectorGrid();
        void drawStreamingConfig();
        void drawCreationWizard();
        void refreshStats();
    };

} // namespace windows
