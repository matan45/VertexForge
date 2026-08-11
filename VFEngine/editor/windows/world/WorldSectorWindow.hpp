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
        bool autoAlignToTerrain = true;
        float terrainTileSize = 0.0f;
        float loadRadius = 4.0f;
        float prefetchRadius = 0.0f; // VK-1591: 0 = same as loadRadius (no prefetch ring)
        float unloadRadius = 5.0f;
        bool gpuObjectStreaming = true;

        int totalSectors = 0;
        int loadedSectors = 0;
        int unloadedSectors = 0;
        int loadingSectors = 0;
        int prefetchedSectors = 0; // VK-1591: bytes resident or in flight, no entities

        struct CachedSectorInfo {
            world::SectorCoord coord;
            bool exists = false;
            world::SectorState state = world::SectorState::Unloaded;
        };
        std::vector<CachedSectorInfo> cachedGrid;
        int cachedTilesPerSector = 4;

        // Grid navigation
        int gridCenterX = 0;
        int gridCenterZ = 0;
        int cameraSectorX = 0;
        int cameraSectorZ = 0;
        bool followCamera = true;
        int gridRange = 8;

        // HLOD state
        bool hlodEnabled = false;
        float hlodTier0Radius = 10.0f;
        float hlodTier1Radius = 20.0f;
        float hlodTier2Radius = 40.0f;
        float hlodTier0Ratio = 0.10f;
        float hlodTier1Ratio = 0.03f;
        float hlodTier2Ratio = 0.01f;
        bool hlodConfigLoaded = false;

        // Async HLOD generation
        bool hlodGenerating = false;
        float hlodGenerationProgress = 0.0f;
        std::string hlodGenerationStage;
        std::vector<world::SectorCoord> hlodPendingSectors;
        int hlodTotalToGenerate = 0;
        int hlodDoneCount = 0;

        // Cached HLOD status (refreshed on timer, not per-frame)
        int cachedHLODCount = 0;
        int cachedHLODTotal = 0;

        // Editable streaming config (loaded once, pushed via SetStreamingConfigCommand)
        world::SectorStreamingConfig editableStreaming;
        bool streamingConfigLoaded = false;

    public:
        WorldSectorWindow() = default;
        ~WorldSectorWindow() override = default;

        void draw() override;
        void show() { visible = true; }

    private:
        void drawWorldInfo();
        void drawSectorGrid();
        void drawStreamingConfig();
        void drawHLODConfig();
        void drawCreationWizard();
        void refreshStats();

        static const std::vector<std::pair<std::wstring, std::wstring>> WORLD_FILE_TYPES;
    };

} // namespace windows
