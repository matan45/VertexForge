#pragma once
#include <memory>

#include "interfaces/ISceneService.hpp"
#include "interfaces/IRenderService.hpp"
#include "interfaces/IInputService.hpp"
#include "interfaces/IWindowStateService.hpp"
#include "interfaces/IAudioService.hpp"
#include "interfaces/IScriptingService.hpp"
#include "interfaces/IProjectService.hpp"
#include "interfaces/IWaterService.hpp"
#include "interfaces/IPhysicsService.hpp"
#include "interfaces/INavmeshService.hpp"
#include "interfaces/IPhysicsAnimationService.hpp"
#include "interfaces/IRenderTextureService.hpp"
#include "interfaces/IControllerService.hpp"
#include "impl/components/IKComponentService.hpp"
#include "events/EventTypes.hpp"

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
}

namespace handlers {

    class RuntimeHandler {
    private:
        std::unique_ptr<core::RuntimeBootstrap> bootstrap;

        std::shared_ptr<services::ISceneService> sceneService;
        std::shared_ptr<services::IRenderService> renderService;
        std::shared_ptr<services::IInputService> inputService;
        std::shared_ptr<services::IWindowStateService> windowStateService;
        std::shared_ptr<services::IAudioService> audioService;
        std::shared_ptr<services::IScriptingService> scriptingService;
        std::shared_ptr<services::IProjectService> projectService;
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

        std::unique_ptr<plugin::PluginManager> pluginManager;

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
    };

}
