#pragma once
#include <filesystem>
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
#include "interfaces/terrain/IOceanService.hpp"
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
#include "interfaces/ai/IEQSService.hpp"
#include "interfaces/volumetric/IVolumetricNavService.hpp"
#include "interfaces/input/IRuntimePickerService.hpp"
#include "interfaces/render/IPluginTextureService.hpp"
#include "interfaces/weather/IWeatherService.hpp"
#include "interfaces/time/ITimeService.hpp"
#include "events/EventTypes.hpp"
#include "events/EventDispatcher.hpp"

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
    class VFXRuntimeServiceImpl;
    class VFXPlayModeHandler;
    class VFXSequenceRuntimeServiceImpl;
    class VFXSequencePlayModeHandler;
    class ConfigService;
    class SaveService;
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
        std::shared_ptr<services::IOceanService> oceanService;
        std::shared_ptr<services::IPhysicsService> physicsService;
        std::shared_ptr<services::IPhysicsAnimationService> physicsAnimationService;
        std::shared_ptr<services::INavmeshService> navmeshService;
        std::shared_ptr<services::IVolumetricNavService> volumetricNavService; // VK-1660
        std::unique_ptr<services::PhysicsPlayModeHandler> physicsPlayModeHandler;
        std::unique_ptr<core::audio::AudioSceneUpdater> audioSceneUpdater;
        // VK-1506: caches the doppler teleport-guard speed from applied audio settings.
        // Declared AFTER audioSceneUpdater so it unsubscribes before the updater is gone.
        events::ScopedSubscription audioSettingsSubscription;
        std::shared_ptr<services::IRenderTextureService> renderTextureService;
        std::unique_ptr<services::RenderTexturePlayModeHandler> renderTexturePlayModeHandler;
        std::shared_ptr<services::IControllerService> controllerService;
        std::shared_ptr<services::IKComponentService> ikComponentService;
        std::shared_ptr<services::IDebugDrawService> debugDrawService;
        std::shared_ptr<services::IAssetLifecycleService> assetLifecycleService;
        std::shared_ptr<services::IWorldSectorService> worldSectorService;
        std::shared_ptr<services::IBehaviorTreeService> behaviorTreeService;
        std::unique_ptr<services::BehaviorTreePlayModeHandler> behaviorTreePlayModeHandler;
        // VK-1660: reset AFTER behaviorTreeService - stopping trees dispatches CancelEQSQueryCommand.
        std::shared_ptr<services::IEQSService> eqsService;
        std::unique_ptr<services::VFXRuntimeServiceImpl> vfxRuntimeService;
        std::unique_ptr<services::VFXPlayModeHandler> vfxPlayModeHandler;
        std::unique_ptr<services::VFXSequenceRuntimeServiceImpl> vfxSequenceRuntimeService;
        std::unique_ptr<services::VFXSequencePlayModeHandler> vfxSequencePlayModeHandler;
        std::shared_ptr<services::IRuntimePickerService> runtimePickerService;
        std::shared_ptr<services::IPluginTextureService> pluginTextureService;
        std::shared_ptr<services::IWeatherService> weatherService;
        std::shared_ptr<services::ITimeService> timeService;
        // VK-1726: the game's state folder (launch::GameStateLocation::stateRoot, worked out in main):
        // %LOCALAPPDATA%\<Publisher>\<Game>, or the game's own folder for a portable build. config.json
        // and saves/ live in it. Empty only when main could not work it out: both services then keep
        // their files beside the .vfproj, as before.
        std::filesystem::path gameStateRoot;
        // VK-1534: the player's settings store (config.json); the Config script natives and the
        // persisted gfx.* preset/display override (re-applied on scene load) use it.
        std::unique_ptr<services::ConfigService> configService;
        // VK-1726: save slots for the Save script natives (it was editor-only, so they threw in
        // shipped games). Reset before the bootstrap: it holds the bootstrap's scripting provider.
        std::unique_ptr<services::SaveService> saveService;

        std::unique_ptr<plugin::PluginManager> pluginManager;

        std::unique_ptr<services::FrameTaskGraph> frameTaskGraph;

        events::SubscriptionToken resizeSubscription;
        events::SubscriptionToken displaySettingsSubscription;
        // VK-1534: re-applies the persisted gfx.* override after each scene load's
        // baked settings, so the player's choice wins and survives scene transitions.
        events::SubscriptionToken sceneLoadedSubscription;

    public:
        explicit RuntimeHandler(std::filesystem::path gameStateRoot);
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
