#pragma once
#include "../../../services/events/EventDispatcher.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace controllers
{
    class OffScreen;
}

namespace core
{
    // VK-1532: kicks off async material-pipeline warm-up when a scene finishes loading, and
    // answers the loading screen's warm-up progress query. Lives in Core because it needs to
    // see both the scene-load notification (Services) and the Graphics RenderPassHandler —
    // Graphics itself cannot reach the EventDispatcher (layering). One instance owned by each
    // bootstrap (editor/runtime).
    class PipelineWarmupAdapter
    {
    public:
        explicit PipelineWarmupAdapter(controllers::OffScreen* offScreen);
        ~PipelineWarmupAdapter();

        PipelineWarmupAdapter(const PipelineWarmupAdapter&) = delete;
        PipelineWarmupAdapter& operator=(const PipelineWarmupAdapter&) = delete;

    private:
        // Phase 2: extra material paths to warm for a scene, read from the PSO manifest baked
        // into the .vfpak (exported builds only). Empty in the editor / when no manifest.
        std::vector<std::string> loadManifestExtraPaths(const std::string& scenePath);

        controllers::OffScreen* offScreen;
        events::ScopedSubscription sceneLoadedSub;

        // Parsed PSO manifest (scene GUID hex -> material GUID hex list), loaded lazily once
        // from the .vfpak. manifestLoaded stays true even when the pak has no manifest so we
        // don't retry the read on every scene load.
        bool manifestLoaded = false;
        std::unordered_map<std::string, std::vector<std::string>> manifestScenes;
    };
}
