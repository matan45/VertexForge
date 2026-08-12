#pragma once

#include "imguiHandler/ImguiWindow.hpp"
#include "events/EventTypes.hpp"
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

        // VK-1594: the bake now runs on the JobSystem behind GenerateAllHLODCommand, so the window
        // only polls GetHLODBakeProgressQuery. The old per-sector, one-command-per-frame loop that
        // lived here still ran each bake synchronously on the UI thread.
        bool hlodBakeWasRunning = false;

        // Cached HLOD status (refreshed on timer, not per-frame)
        int cachedHLODCount = 0;
        int cachedHLODTotal = 0;

        // Editable streaming config (loaded once, pushed via SetStreamingConfigCommand).
        // VK-1595: seeded from GetPersistedStreamingConfigQuery, NOT from
        // GetWorldStreamingStatsQuery - the latter reports the effective config, so with a session
        // override active the first slider drag would copy the override into the .vfworld.
        world::SectorStreamingConfig editableStreaming;
        bool streamingConfigLoaded = false;

        // VK-1595: the session override's working copy. Never persisted.
        world::SectorStreamingConfig overrideStreaming;
        bool overrideLoaded = false;

        // Both "loaded once" latches above describe ONE world. Opening another must invalidate
        // them, or the next slider drag pushes the previous world's whole config into this one and
        // Save World persists it.
        ::events::SubscriptionToken worldLoadedToken;

    public:
        WorldSectorWindow();
        ~WorldSectorWindow() override;

        void draw() override;
        void show() { visible = true; }

    private:
        void drawWorldInfo();
        void drawSectorGrid();
        void drawStreamingConfig();
        // VK-1595: one slider block, driven twice - once for the persisted config and once for the
        // session override - so the two can never drift apart. Callers wrap it in ImGui::PushID.
        // `sessionOverride` disables the fields an override cannot actually reach.
        static bool drawStreamingSliders(world::SectorStreamingConfig& config, bool sessionOverride);
        void drawStreamingDebugSection();
        void drawHLODConfig();
        // Drop every "loaded once" editing cache, so the next draw re-seeds from the new world.
        void invalidateConfigCaches();
        void drawCreationWizard();
        void refreshStats();

        static const std::vector<std::pair<std::wstring, std::wstring>> WORLD_FILE_TYPES;
    };

} // namespace windows
