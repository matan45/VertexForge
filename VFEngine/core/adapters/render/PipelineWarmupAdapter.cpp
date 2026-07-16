#include "PipelineWarmupAdapter.hpp"
#include "../../controllers/OffScreen.hpp"
#include "../../../graphics/render/RenderPassHandler.hpp"
#include "../../../services/events/scene/ScenePersistenceEvents.hpp"
#include "../../../services/events/render/PipelineWarmupEvents.hpp"

#include "material/PipelineWarmupManifest.hpp"
#include "resource/VirtualFileSystem.hpp"
#include "resource/PathResolver.hpp"
#include "asset/AssetRef.hpp"

#include <string_view>
#include <utility>

namespace core
{
    PipelineWarmupAdapter::PipelineWarmupAdapter(controllers::OffScreen* offScreen)
        : offScreen(offScreen)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Warm material pipelines in the background once a scene is fully loaded. We trigger on
        // SceneLoadedNotification (published at the very end of finishLoad) rather than
        // SceneLoadingCompletedNotification, because the tail of finishLoad synchronously applies
        // IBL/display/atmosphere settings that reinit the mesh pipeline layout the warm workers
        // read. Starting after those settle means warm-up runs against a stable config and never
        // has to be drained mid-load. SceneLoadedNotification is only published on success.
        sceneLoadedSub = events::ScopedSubscription(
            dispatcher.subscribe<events::scene::SceneLoadedNotification>(
                [this](const events::scene::SceneLoadedNotification& n)
                {
                    auto* handler = this->offScreen ? this->offScreen->getRenderPassHandler() : nullptr;
                    if (!handler)
                    {
                        return;
                    }
                    handler->beginPipelineWarmup(loadManifestExtraPaths(n.scenePath));
                }));

        // The loading screen polls warm-up progress through this query.
        dispatcher.registerQueryHandler<events::render::GetPipelineWarmupStatsQuery>(
            [this](const events::render::GetPipelineWarmupStatsQuery&) -> services::PipelineWarmupStats
            {
                auto* handler = this->offScreen ? this->offScreen->getRenderPassHandler() : nullptr;
                return handler ? handler->getPipelineWarmupStats() : services::PipelineWarmupStats{};
            });
    }

    PipelineWarmupAdapter::~PipelineWarmupAdapter()
    {
        events::EventDispatcher::instance()
            .unregisterQueryHandler<events::render::GetPipelineWarmupStatsQuery>();
        // sceneLoadedSub unsubscribes itself.
    }

    std::vector<std::string> PipelineWarmupAdapter::loadManifestExtraPaths(const std::string& scenePath)
    {
        // Only exported builds carry a .vfpak PSO manifest. In the editor the registry walk
        // (Phase 1) already covers the loaded scene.
        if (!resource::PathResolver::isExportedMode())
        {
            return {};
        }

        // Lazily read + parse the manifest once from the pak.
        if (!manifestLoaded)
        {
            manifestLoaded = true;
            const auto bytes = resource::VirtualFileSystem::instance().readFile(material::kPsoManifestEntry);
            if (!bytes.empty())
            {
                const std::string_view json(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                manifestScenes = material::PipelineWarmupManifest::fromJson(json).scenes;
            }
        }

        if (manifestScenes.empty())
        {
            return {};
        }

        // Resolve the loaded scene to its GUID (same normalization AssetRefs use everywhere),
        // then map the manifest's material GUIDs back to the paths MaterialShaderCache keys on.
        const asset::AssetRef sceneRef = asset::AssetRef::fromPath(scenePath);
        if (!sceneRef.isValid())
        {
            return {};
        }
        const auto it = manifestScenes.find(sceneRef.getGUID().toString());
        if (it == manifestScenes.end())
        {
            return {}; // no manifest entry — Phase 1 registry walk still covers the scene
        }

        std::vector<std::string> paths;
        paths.reserve(it->second.size());
        for (const auto& materialGuidHex : it->second)
        {
            const asset::AssetRef matRef = asset::AssetRef::fromHexString(materialGuidHex);
            if (!matRef.isValid())
            {
                continue;
            }
            const std::string& path = matRef.resolve();
            if (!path.empty())
            {
                paths.push_back(path);
            }
        }
        return paths;
    }
}
