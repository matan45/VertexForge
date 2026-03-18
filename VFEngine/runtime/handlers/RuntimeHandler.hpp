#pragma once
#include <memory>

#include "interfaces/project/ISceneService.hpp"
#include "interfaces/render/IRenderService.hpp"
#include "interfaces/input/IInputService.hpp"
#include "interfaces/input/IActionMappingService.hpp"
#include "interfaces/editor/IWindowStateService.hpp"
#include "interfaces/audio/IAudioService.hpp"
#include "interfaces/scripting/IScriptingService.hpp"
#include "interfaces/project/IProjectService.hpp"
#include "interfaces/terrain/ITerrainService.hpp"
#include "interfaces/terrain/IWaterService.hpp"
#include "interfaces/physics/IPhysicsService.hpp"
#include "interfaces/navmesh/INavmeshService.hpp"
#include "interfaces/physics/IPhysicsAnimationService.hpp"
#include "interfaces/render/IRenderTextureService.hpp"
#include "interfaces/physics/IControllerService.hpp"
#include "interfaces/render/IDebugDrawService.hpp"
#include "impl/components/IKComponentService.hpp"
#include "interfaces/lifecycle/IAssetLifecycleService.hpp"
#include "interfaces/world/IWorldSectorService.hpp"
#include "interfaces/ai/IBehaviorTreeService.hpp"
#include "events/EventTypes.hpp"

namespace services {
    class FrameTaskGraph;
}

namespace plugin {
    class PluginManager;
}

namespace core::audio {
    class AudioSceneUpdater;
}

namespace core {
    class RuntimeBootstrap;
}

namespace services {
    class PhysicsPlayModeHandler;
    class RenderTexturePlayModeHandler;
    class BehaviorTreePlayModeHandler;
}

namespace handlers {

    class RuntimeHandler {
    private:
        std::unique_ptr<core::RuntimeBootstrap> bootstrap;

        std::shared_ptr<services::ISceneService> sceneService;
        std::shared_ptr<services::IRenderService> renderService;
        std::shared_ptr<services::IInputService> inputService;
        std::shared_ptr<services::IActionMappingService> actionMappingService;
        std::shared_ptr<services::IWindowStateService> windowStateService;
        std::shared_ptr<services::IAudioService> audioService;
        std::shared_ptr<services::IScriptingService> scriptingService;
        std::shared_ptr<services::IProjectService> projectService;
        std::shared_ptr<services::ITerrainService> terrainService;
        std::shared_ptr<services::IWaterService> waterService;
        std::shared_ptr<services::IPhysicsService> physicsService;
        std::shared_ptr<services::IPhysicsAnimationService> physicsAnimationService;
        std::shared_ptr<services::INavmeshService> navmeshService;
        std::unique_ptr<services::PhysicsPlayModeHandler> physicsPlayModeHandler;
        std::unique_ptr<core::audio::AudioSceneUpdater> audioSceneUpdater;
        std::shared_ptr<services::IRenderTextureService> renderTextureService;
        std::unique_ptr<services::RenderTexturePlayModeHandler> renderTexturePlayModeHandler;
        std::shared_ptr<services::IControllerService> controllerService;
        std::shared_ptr<services::IKComponentService> ikComponentService;
        std::shared_ptr<services::IDebugDrawService> debugDrawService;
        std::shared_ptr<services::IAssetLifecycleService> assetLifecycleService;
        std::shared_ptr<services::IWorldSectorService> worldSectorService;
        std::shared_ptr<services::IBehaviorTreeService> behaviorTreeService;
        std::unique_ptr<services::BehaviorTreePlayModeHandler> behaviorTreePlayModeHandler;

        std::unique_ptr<plugin::PluginManager> pluginManager;

        std::unique_ptr<services::FrameTaskGraph> frameTaskGraph;

        events::SubscriptionToken resizeSubscription;

    public:
        explicit RuntimeHandler();
        ~RuntimeHandler();

        RuntimeHandler(const RuntimeHandler&) = delete;
        RuntimeHandler& operator=(const RuntimeHandler&) = delete;

        void init();
        void run() const;
        void cleanUp();
        bool loadProject(const std::string& projectPath);

    private:
        void initializeServices();
        void setupEventSubscriptions();
        void cleanupEventSubscriptions();
        void buildFrameTaskGraph();
    };

}
