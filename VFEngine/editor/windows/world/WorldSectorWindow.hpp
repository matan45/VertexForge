#pragma once

#include "imguiHandler/ImguiWindow.hpp"
#include "events/EventTypes.hpp"
#include "world/WorldTypes.hpp"
#include "world/SectorDataLayerOps.hpp"
#include "world/SectorRepartitionTypes.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
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

        // ── VK-1596: Data Layers ──────────────────────────────────────────────────────
        // Refreshed on the same 0.25s timer as the sector stats. Polled unconditionally rather
        // than only while the tab is open: the Sector Grid tooltip and highlight read the derived
        // cache below, so gating the poll would show stale layers there. One O(sectors) in-memory
        // walk is noise next to the ~2 dispatcher round-trips per grid cell refreshStats already
        // makes.
        world::DataLayerInventory cachedLayers;

        // Inverted from cachedLayers once per refresh so the grid tooltip and highlight cost no
        // extra queries. Keyed only by sectors that actually carry a layer, so it is bounded by the
        // loaded-sector count rather than by the ~289 visible grid cells.
        std::unordered_map<world::SectorCoord, std::vector<std::string>, world::SectorCoordHash>
            cachedSectorLayers;

        // A NAME, not a row index: the table is rebuilt from a fresh, re-sorted summary every
        // 0.25s, so an index would silently point at a different layer.
        std::string selectedLayer;
        char newLayerName[64] = "";
        std::string layerError;
        int exportSectorIndex = 0;

        // Bytes an un-check removed this session, keyed layer -> coord -> blob, so re-checking
        // restores them instead of silently discarding work. Export is the durable backup; this is
        // the cheap safety net for a mis-click. Dropped with the other per-world caches.
        std::unordered_map<std::string,
                           std::unordered_map<world::SectorCoord, std::vector<uint8_t>,
                                              world::SectorCoordHash>>
            detachedLayers;

        // Latched when the delete modal opens. The summary repolls every 0.25s and streaming can
        // change the loaded set underneath an open dialog, so the confirmation must destroy exactly
        // what it named. Same reasoning as the content browser's delete-dependents latch.
        std::string pendingDeleteLayer;
        uint32_t pendingDeleteSectorCount = 0;
        uint64_t pendingDeleteBytes = 0;

        // ── VK-1598: repartition / convert-to-flat ────────────────────────────────────
        // The candidate config. Seeded from the world's current one each time the modal opens, so
        // the dialog always starts from what the world actually is rather than from the last thing
        // that was typed into it.
        float repartitionSectorSize = 128.0f;
        int repartitionTilesPerSector = 4;
        bool repartitionAlignToTerrain = false;
        bool repartitionSeeded = false;

        // The dry run is an explicit button, never a per-frame poll: PreviewRepartitionQuery reads
        // and parses every .vfsector in the world.
        world::RepartitionSummary repartitionPreview;
        bool repartitionPreviewValid = false;
        std::string repartitionStatus;
        bool repartitionStatusIsError = false;

        // Latched when the confirm modal opens, for the same reason the delete-layer modal latches
        // its target: the dialog must apply exactly the config it described.
        world::SectorConfig pendingRepartitionConfig;

        bool openRepartitionModal = false;
        bool openFlattenModal = false;
        bool openRebakePrompt = false;
        bool openSaveScenePrompt = false;
        uint32_t flattenedEntityCount = 0;

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

        // VK-1596 - defined in WorldSectorWindowDataLayers.cpp to keep this window's main .cpp
        // readable, following EditorPreferencesWindowSections.cpp.
        void drawDataLayers();
        void refreshDataLayers();
        void drawDeleteLayerModal();
        // Presence toggle across every loaded sector: `present` ensures the layer exists there,
        // otherwise removes it (stashing the bytes in detachedLayers first).
        void applyLayerPresence(const std::string& layerName, bool present);
        [[nodiscard]] const world::DataLayerSummary* findCachedLayer(const std::string& name) const;

        // VK-1598 - defined in WorldSectorWindowRepartition.cpp, same split as the Data Layers tab.
        // drawRepartitionSection draws the Streaming Config tab's entry point; the three modals are
        // opened from draw() so OpenPopup and BeginPopupModal share a popup-stack ID scope.
        void drawRepartitionSection();
        void drawRepartitionModals();
        [[nodiscard]] world::SectorConfig currentRepartitionConfig() const;
        void seedRepartitionFromWorld();

        static const std::vector<std::pair<std::wstring, std::wstring>> WORLD_FILE_TYPES;
        static const std::vector<std::pair<std::wstring, std::wstring>> LAYER_FILE_TYPES;
    };

} // namespace windows
