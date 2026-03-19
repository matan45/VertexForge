#pragma once

#include "imguiHandler/ImguiWindow.hpp"
#include "world/WorldTypes.hpp"
#include <string>
#include <utility>
#include <vector>

namespace windows
{
    class WorldSectorWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        bool visible = false;
        float refreshTimer = 0.0f;
        static constexpr float REFRESH_INTERVAL = 0.25f;

        bool showCreationWizard = false;
        char worldName[128] = "New World";
        float sectorSize = 128.0f;
        int tilesPerSector = 4;
        float loadRadius = 4.0f;
        float unloadRadius = 5.0f;
        bool gpuObjectStreaming = true;

        int totalSectors = 0;
        int loadedSectors = 0;
        int unloadedSectors = 0;
        int loadingSectors = 0;

        struct CachedSectorInfo {
            world::SectorCoord coord;
            bool exists = false;
            world::SectorState state = world::SectorState::Unloaded;
        };
        std::vector<CachedSectorInfo> cachedGrid;

    public:
        WorldSectorWindow() = default;
        ~WorldSectorWindow() override = default;

        void draw() override;
        void show() { visible = true; }

    private:
        void drawWorldInfo();
        void drawSectorGrid();
        void drawStreamingConfig();
        void drawCreationWizard();
        void refreshStats();

        static const std::vector<std::pair<std::wstring, std::wstring>> WORLD_FILE_TYPES;
    };

} // namespace windows
