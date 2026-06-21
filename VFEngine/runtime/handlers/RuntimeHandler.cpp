#include "print/Log.hpp"
#include "RuntimeHandler.hpp"
#include "runtime/RuntimeBootstrap.hpp"
#include "impl/project/SceneServiceImpl.hpp"
#include "impl/render/RuntimeRenderServiceImpl.hpp"
#include "impl/input/InputServiceImpl.hpp"
#include "impl/input/ActionMappingServiceImpl.hpp"
#include "impl/editor/WindowStateServiceImpl.hpp"
#include "impl/audio/AudioServiceImpl.hpp"
#include "impl/scripting/ScriptingServiceImpl.hpp"
#include "impl/project/ProjectServiceImpl.hpp"
#include "impl/scene/TerrainService.hpp"
#include "impl/scene/OceanService.hpp"
#include "impl/weather/WeatherServiceImpl.hpp"
#include "impl/physics/PhysicsServiceImpl.hpp"
#include "impl/physics/PhysicsAnimationServiceImpl.hpp"
#include "impl/navmesh/NavmeshServiceImpl.hpp"
#include "impl/physics/PhysicsPlayModeHandler.hpp"
#include "impl/physics/ControllerServiceImpl.hpp"
#include "../audio/AudioSceneUpdater.hpp"
#include "../audio/ReverbZoneManager.hpp"
#include "../adapters/terrain/TerrainRenderAdapter.hpp"
#include "../adapters/terrain/OceanRenderAdapter.hpp"
#include "impl/render/RenderTextureServiceImpl.hpp"
#include "impl/render/RenderTexturePlayModeHandler.hpp"
#include "impl/render/DebugDrawServiceImpl.hpp"
#include "impl/render/PluginTextureServiceImpl.hpp"
#include "impl/lifecycle/AssetLifecycleServiceImpl.hpp"
#include "impl/world/WorldSectorServiceImpl.hpp"
#include "impl/ai/BehaviorTreeServiceImpl.hpp"
#include "impl/ai/BehaviorTreePlayModeHandler.hpp"
#include "impl/input/RuntimePickerServiceImpl.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ApplicationEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/weather/WeatherEvents.hpp"
#include "events/terrain/OceanEvents.hpp"
#include "resource/PathResolver.hpp"
#include "resource/VirtualFileSystem.hpp"
#include <filesystem>
#include "time/Timer.hpp"
#include "core/PluginManager.hpp"
#include "api/PluginVersion.hpp"
#include "impl/threading/FrameTaskGraph.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/CoreComponents.hpp"
#include "events/render/RenderEvents.hpp"
#include "math/TransformUtils.hpp"

namespace handlers {

    RuntimeHandler::RuntimeHandler()
        : bootstrap(std::make_unique<core::RuntimeBootstrap>()) {}

    RuntimeHandler::~RuntimeHandler() = default;

    void RuntimeHandler::init() {
        resource::PathResolver::initialize();
        resource::VirtualFileSystem::instance().initialize();
        bootstrap->init();

        // Runtime is always in play mode - hide editor-only overlays (grid, gizmos, etc.)
        if (auto* offScreen = bootstrap->getOffScreenProvider())
        {
            offScreen->setPlayMode(true);
        }

        initializeServices();

        pluginManager = std::make_unique<plugin::PluginManager>(std::unordered_set<std::string>{
            std::string(plugin::capability::audio),
            std::string(plugin::capability::physics),
            std::string(plugin::capability::scripting),
            std::string(plugin::capability::terrain),
            std::string(plugin::capability::input),
            std::string(plugin::capability::navmesh),
            std::string(plugin::capability::vfx)
        });
        pluginManager->loadAll(plugin::PluginManager::resolvePluginsDirectory());
        pluginManager->initializeAll();

        // Build the frame task graph with dependency-based parallel execution
        buildFrameTaskGraph();

        bootstrap->setFrameCallback([this]() {
            frameTaskGraph->execute();
        });
        
        setupEventSubscriptions();
    }

    void RuntimeHandler::run() const {
        bootstrap->run();
    }

    void RuntimeHandler::cleanUp() {
        if (frameTaskGraph) {
            frameTaskGraph->unregisterEventHandlers();
            frameTaskGraph.reset();
        }

        pluginManager.reset();

        cleanupEventSubscriptions();

        if (physicsPlayModeHandler)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::editor::EditorModeChangedNotification notification;
            notification.previousMode = services::EditorMode::Play;
            notification.currentMode = services::EditorMode::Edit;
            dispatcher.publish(notification);
        }

        physicsPlayModeHandler.reset();
        renderTexturePlayModeHandler.reset();
        worldSectorService.reset();
        assetLifecycleService.reset();
        controllerService.reset();
        behaviorTreePlayModeHandler.reset();
        behaviorTreeService.reset();
        ikComponentService.reset();
        physicsAnimationService.reset();
        physicsService.reset();
        navmeshService.reset();
        weatherService.reset();
        oceanService.reset();
        terrainService.reset();
        projectService.reset();
        audioSceneUpdater.reset();
        audioService.reset();
        scriptingService.reset();
        pluginTextureService.reset();
        renderTextureService.reset();
        debugDrawService.reset();
        renderService.reset();
        sceneService.reset();
        windowStateService.reset();
        actionMappingService.reset();
        inputService.reset();

        bootstrap->cleanUp();
    }

    bool RuntimeHandler::loadProject(const std::string& projectPath) {
        auto& dispatcher = events::EventDispatcher::instance();

        events::project::LoadProjectCommand loadCmd;
        loadCmd.filePath = projectPath;
        if (!dispatcher.execute(loadCmd)) {
            vfLogError("Failed to load project file: {}", projectPath);
            dispatcher.execute(events::scene::NewSceneCommand{});
            return false;
        }

        auto projectOpt = dispatcher.query(events::project::GetCurrentProjectQuery{});
        if (!projectOpt) {
            vfLogError("Failed to get project configuration");
            dispatcher.execute(events::scene::NewSceneCommand{});
            return false;
        }

        // Version-skew check: exported projects are stamped with the exporting
        // editor's plugin API version. A mismatch means the shipped plugin DLLs
        // were built against a different engine build and will be rejected.
        if (projectOpt->pluginApiVersion.has_value() &&
            *projectOpt->pluginApiVersion != plugin::VF_PLUGIN_API_VERSION) {
            vfLogWarning("This game was exported with plugin API v{} but the runtime expects v{} — "
                         "plugins may fail to load (re-export the game)",
                         *projectOpt->pluginApiVersion, plugin::VF_PLUGIN_API_VERSION);
        }

        // Set window title from project config
        bootstrap->setWindowTitle(projectOpt->projectName);

        // Try to load a window icon (.vfImage) from the assets folder (optional)
        if (!projectOpt->exeIconPath.empty())
        {
            std::filesystem::path iconBase =
                std::filesystem::path(projectOpt->workingDirectory) / projectOpt->exeIconPath;
            if (std::filesystem::exists(iconBase))
            {
                bootstrap->setWindowIcon(iconBase.string());
            }
            else
            {
                std::filesystem::path vfImageIcon = iconBase;
                vfImageIcon.replace_extension(".vfImage");
                if (std::filesystem::exists(vfImageIcon))
                {
                    bootstrap->setWindowIcon(vfImageIcon.string());
                }
            }
        }

        std::filesystem::path scenePath =
            std::filesystem::path(projectOpt->workingDirectory) / projectOpt->startupScene;

        if (!std::filesystem::exists(scenePath)) {
            vfLogError("Startup scene not found: {}", scenePath.string());
            dispatcher.execute(events::scene::NewSceneCommand{});
            return false;
        }

        events::scene::LoadSceneCommand sceneCmd;
        sceneCmd.filePath = scenePath.string();
        if (!dispatcher.execute(sceneCmd)) {
            vfLogError("Failed to queue startup scene load: {}", scenePath.string());
            dispatcher.execute(events::scene::NewSceneCommand{});
            return false;
        }

        // PhysicsPlayModeHandler listens for EditorModeChangedNotification
        if (physicsPlayModeHandler)
        {
            events::editor::EditorModeChangedNotification notification;
            notification.previousMode = services::EditorMode::Edit;
            notification.currentMode = services::EditorMode::Play;
            dispatcher.publish(notification);
        }

        return true;
    }

    void RuntimeHandler::initializeServices() {
        auto sceneServiceImpl = std::make_shared<services::SceneServiceImpl>(
            bootstrap->getSceneGraphSystem(),
            bootstrap->getAnimatorProvider(),
            bootstrap->getSocketProvider()
        );
        // Spread deferred scene loads across frames so a loading screen can
        // animate during transitions instead of the game hitching for one frame
        // (VK-1268). The render loop keeps presenting between spawn batches.
        // (Set on the concrete impl; not part of the ISceneService interface.)
        sceneServiceImpl->setIncrementalLoadBudget(64);
        sceneService = sceneServiceImpl;
        renderService = std::make_shared<services::RuntimeRenderServiceImpl>(
            bootstrap->getOffScreenProvider(),
            bootstrap->getPostProcessProvider()
        );
        inputService = std::make_shared<services::InputServiceImpl>(bootstrap->getWindow());
        actionMappingService = std::make_shared<services::ActionMappingServiceImpl>();
        windowStateService = std::make_shared<services::WindowStateServiceImpl>(bootstrap->getWindow());
        audioService = std::make_shared<services::AudioServiceImpl>(bootstrap->getAudioProvider());
        audioSceneUpdater = std::make_unique<core::audio::AudioSceneUpdater>();
        auto* reverbZoneMgr = static_cast<core::audio::ReverbZoneManager*>(
            bootstrap->getAudioProvider()->getReverbZoneManager());
        audioSceneUpdater->setReverbZoneManager(reverbZoneMgr);

        scriptingService = std::make_shared<services::ScriptingServiceImpl>(
            bootstrap->getScriptingProvider(),
            bootstrap->getSceneGraphSystem()
        );

        projectService = std::make_shared<services::ProjectServiceImpl>();

        auto terrainServiceImpl = std::make_shared<services::TerrainService>(bootstrap->getSceneGraphSystem());
        terrainService = terrainServiceImpl;
        if (auto* terrainAdapter = bootstrap->getTerrainRenderAdapterInternal())
        {
            terrainAdapter->setTerrainService(terrainServiceImpl.get());
        }
        if (auto* physicsProvider = bootstrap->getPhysicsProvider())
        {
            terrainServiceImpl->setPhysicsProvider(physicsProvider);
        }

        auto oceanServiceImpl = std::make_shared<services::OceanService>(bootstrap->getSceneGraphSystem());
        oceanService = oceanServiceImpl;
        oceanServiceImpl->setPhysicsProvider(bootstrap->getPhysicsProvider());
        if (auto* oceanAdapter = bootstrap->getOceanRenderAdapterInternal())
        {
            oceanAdapter->setOceanService(oceanServiceImpl.get());
        }

        // Weather runs in shipped games too: scripts drive it (Weather natives) and the
        // ocean's weather-driven sea state queries it. Without this, both silently no-op
        // outside the editor.
        weatherService = std::make_shared<services::WeatherServiceImpl>(
            bootstrap->getVFXRuntimeProvider());

        if (auto* physicsProvider = bootstrap->getPhysicsProvider())
        {
            physicsService = std::make_shared<services::PhysicsServiceImpl>(physicsProvider);
            physicsAnimationService = std::make_shared<services::PhysicsAnimationServiceImpl>(physicsProvider);
            physicsPlayModeHandler = std::make_unique<services::PhysicsPlayModeHandler>(physicsProvider);
            physicsPlayModeHandler->setOceanService(oceanServiceImpl.get());
            physicsPlayModeHandler->subscribeToEvents();

            // Wire script onFixedUpdate to run after each physics sub-step
            if (scriptingService)
            {
                physicsPlayModeHandler->setScriptFixedUpdateCallback([this](float fixedDt)
                {
                    scriptingService->fixedUpdateScripts(fixedDt);
                });
            }
        }

        if (auto* navmeshProvider = bootstrap->getNavmeshProvider())
        {
            navmeshService = std::make_shared<services::NavmeshServiceImpl>(navmeshProvider);
        }

        controllerService = std::make_shared<services::ControllerServiceImpl>(bootstrap->getPhysicsProvider());

        if (auto* ikProvider = bootstrap->getIKProvider())
        {
            ikComponentService = std::make_shared<services::IKComponentService>(ikProvider);
        }

        renderTextureService = std::make_shared<services::RenderTextureServiceImpl>(
            bootstrap->getRenderTextureProvider()
        );

        debugDrawService = std::make_shared<services::DebugDrawServiceImpl>(
            bootstrap->getDebugDrawProvider()
        );

        assetLifecycleService = std::make_shared<services::AssetLifecycleServiceImpl>();

        worldSectorService = std::make_shared<services::WorldSectorServiceImpl>(
            bootstrap->getSceneGraphSystem()
        );

        behaviorTreeService = std::make_shared<services::BehaviorTreeServiceImpl>(
            bootstrap->getBehaviorTreeProvider()
        );

        runtimePickerService = std::make_shared<services::RuntimePickerServiceImpl>(
            bootstrap->getRuntimePickerProvider()
        );

        // Plugin world-mask / texture handlers (e.g. fog-of-war): plugins and scripts call
        // these in runtime too, so the handlers must be registered here — not just in the
        // editor — or WorldMask::sample and bindWorldMask throw at the dispatcher.
        pluginTextureService = std::make_shared<services::PluginTextureServiceImpl>(
            bootstrap->getPluginTextureProvider()
        );

        if (auto* btProvider = bootstrap->getBehaviorTreeProvider())
        {
            behaviorTreePlayModeHandler = std::make_unique<services::BehaviorTreePlayModeHandler>(btProvider);
            behaviorTreePlayModeHandler->subscribeToEvents();
        }

        if (auto* rttProvider = bootstrap->getRenderTextureProvider())
        {
            renderTexturePlayModeHandler = std::make_unique<services::RenderTexturePlayModeHandler>(rttProvider);
            renderTexturePlayModeHandler->subscribeToEvents();
        }

        if (auto* runtimeRenderService =
                dynamic_cast<services::RuntimeRenderServiceImpl*>(renderService.get()))
        {
            runtimeRenderService->setPreOffscreenRenderCallback([this]()
            {
                if (renderTexturePlayModeHandler)
                {
                    float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                    renderTexturePlayModeHandler->update(dt);
                }
            });
        }

        sceneService->registerEventHandlers();
        projectService->registerEventHandlers();
        renderService->registerEventHandlers();
        inputService->registerEventHandlers();
        actionMappingService->registerEventHandlers();
        windowStateService->registerEventHandlers();
        static_cast<services::AudioServiceImpl*>(audioService.get())->registerEventHandlers();
        static_cast<services::ScriptingServiceImpl*>(scriptingService.get())->registerEventHandlers();
        terrainService->registerEventHandlers();
        oceanService->registerEventHandlers();
        weatherService->registerEventHandlers();
        if (physicsService)
        {
            physicsService->registerEventHandlers();
        }
        if (physicsAnimationService)
        {
            physicsAnimationService->registerEventHandlers();
        }
        if (navmeshService)
        {
            navmeshService->registerEventHandlers();
        }
        renderTextureService->registerEventHandlers();
        debugDrawService->registerEventHandlers();
        assetLifecycleService->registerEventHandlers();
        worldSectorService->registerEventHandlers();
        controllerService->registerEventHandlers();
        behaviorTreeService->registerEventHandlers();
        runtimePickerService->registerEventHandlers();
        pluginTextureService->registerEventHandlers();
        if (ikComponentService)
        {
            ikComponentService->registerEventHandlers();
        }
    }

    void RuntimeHandler::setupEventSubscriptions()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        resizeSubscription = dispatcher.subscribe<events::application::WindowResizedNotification>(
            [this](const events::application::WindowResizedNotification&) {
                bootstrap->triggerResize();
            });

        displaySettingsSubscription = dispatcher.subscribe<events::application::ApplyDisplaySettingsNotification>(
            [this](const events::application::ApplyDisplaySettingsNotification& n) {
                bootstrap->applyDisplaySettings(n.presentMode, n.msaa);
            });
    }

    void RuntimeHandler::cleanupEventSubscriptions()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (resizeSubscription.isValid()) {
            dispatcher.unsubscribe(resizeSubscription);
            resizeSubscription = {};
        }

        if (displaySettingsSubscription.isValid()) {
            dispatcher.unsubscribe(displaySettingsSubscription);
            displaySettingsSubscription = {};
        }
    }

    void RuntimeHandler::buildFrameTaskGraph()
    {
        frameTaskGraph = std::make_unique<services::FrameTaskGraph>();

        // Runtime is always in play mode - all tasks are unconditional

        frameTaskGraph->addTask("Scene", [this]() {
            if (sceneService) sceneService->update();
        });

        frameTaskGraph->addTask("Input", [this]() {
            if (inputService) inputService->update();
        });

        frameTaskGraph->addTask("WindowState", [this]() {
            if (windowStateService) windowStateService->update();
        });

        frameTaskGraph->addTask("PhysicsKick", [this]() {
            if (physicsPlayModeHandler) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                physicsPlayModeHandler->kickUpdate(dt);
            }
        });

        frameTaskGraph->addTask("PhysicsSync", [this]() {
            if (physicsPlayModeHandler) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                physicsPlayModeHandler->syncUpdate(dt);
            }
        });

        frameTaskGraph->addTask("Scripts", [this]() {
            if (scriptingService) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                // Safety net: a catchable mType (interpreter) error in any script
                // is logged and the game continues. Native JIT faults still go to
                // the crash handler (see CrashHandler).
                try { scriptingService->updateScripts(dt); }
                catch (const std::exception& e) { vfLogError("[Script] updateScripts threw: {}", e.what()); }
            }
        });

        frameTaskGraph->addTask("Controllers", [this]() {
            if (controllerService) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                controllerService->applyControllerMovement(dt);
            }
        });

        frameTaskGraph->addTask("BehaviorTrees", [this]() {
            if (behaviorTreeService) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                behaviorTreeService->updateAll(dt);
            }
        });

        frameTaskGraph->addTask("Navmesh", [this]() {
            if (navmeshService) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                navmeshService->update(dt, true);
            }
        });

        // Pinned to the main thread: updateListenerFromPrimaryCamera() dispatches
        // SetListenerPositionCommand into the audio command queue, which is not safe to
        // invoke from an enkiTS worker (see the editor's note in EditorFrameTaskGraph).
        // With per-layer barriers removed (VK-1385) a worker would otherwise run this.
        frameTaskGraph->addTask("AudioListener", [this]() {
            if (audioSceneUpdater) {
                audioSceneUpdater->updateListenerFromPrimaryCamera();
            }
        }, threading::JobPriority::NORMAL, /*mainThread=*/true);

        frameTaskGraph->addTask("Weather", [this]() {
            float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
            events::weather::UpdateWeatherCommand cmd;
            cmd.deltaTime = dt;
            events::EventDispatcher::instance().execute(cmd);
        });

        frameTaskGraph->addTask("Ocean", [this]() {
            float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
            events::ocean::UpdateOceanCommand cmd;
            cmd.deltaTime = dt;
            events::EventDispatcher::instance().execute(cmd);
        });

        frameTaskGraph->addTask("WorldSector", [this]() {
            if (worldSectorService) worldSectorService->update();
        });

        frameTaskGraph->addTask("AssetLifecycle", [this]() {
            if (assetLifecycleService) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                assetLifecycleService->update(dt);
            }
        });

        frameTaskGraph->addTask("Plugins", [this]() {
            if (pluginManager) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                pluginManager->updateAll(dt);
            }
        });

        // Dependencies
        frameTaskGraph->addDependency("Weather", "Scene");
        frameTaskGraph->addDependency("Ocean", "Weather");
        frameTaskGraph->addDependency("PhysicsKick", "Scene");
        frameTaskGraph->addDependency("PhysicsKick", "Input");
        frameTaskGraph->addDependency("PhysicsKick", "WindowState");
        frameTaskGraph->addDependency("PhysicsSync", "PhysicsKick");
        frameTaskGraph->addDependency("Scripts", "PhysicsSync");
        frameTaskGraph->addDependency("Controllers", "Scripts");
        frameTaskGraph->addDependency("BehaviorTrees", "Controllers");
        frameTaskGraph->addDependency("Navmesh", "BehaviorTrees");
        frameTaskGraph->addDependency("AudioListener", "Scripts");

        // === Full frame pipeline tasks ===

        auto sceneGraphFn = bootstrap->getSceneGraphUpdateFn();
        frameTaskGraph->addTask("Transforms", [sceneGraphFn]() {
            if (sceneGraphFn) sceneGraphFn();
        });

        // Pinned to the main thread: prepares cameras and dispatches render commands
        // (offScreen->prepareCameras, UpdateMeshCamera/IBL, getViewportTexture) - the
        // runtime equivalent of the editor's main-thread ViewPort camera prep. With
        // per-layer barriers removed (VK-1385) this would otherwise run on a worker.
        frameTaskGraph->addTask("PostUpdate", [this]() {
            // Late scripts
            if (scriptingService) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                try { scriptingService->lateUpdateScripts(dt); }
                catch (const std::exception& e) { vfLogError("[Script] lateUpdateScripts threw: {}", e.what()); }
            }

            // Camera preparation
            if (auto* offScreen = bootstrap->getOffScreenProvider()) {
                offScreen->prepareCameras();
            }

            // Push primary camera matrices
            auto& registry = scene::EntityRegistry::getRegistry();
            auto cameraView = registry.view<components::CameraComponent, components::WorldTransformComponent>();

            for (auto entity : cameraView) {
                auto& camComp = cameraView.get<components::CameraComponent>(entity);
                if (!camComp.isPrimary) continue;

                auto& worldTransform = cameraView.get<components::WorldTransformComponent>(entity);

                // VK-1416: when a script owns the camera matrices (viewMatrixOverride), skip the
                // engine recompute and use the stored view/projection as-is.
                glm::vec3 cameraPos;
                if (!camComp.viewMatrixOverride) {
                    if (windowStateService) {
                        uint32_t w = windowStateService->getWidth();
                        uint32_t h = windowStateService->getHeight();
                        if (w > 0 && h > 0) {
                            float newAspect = static_cast<float>(w) / static_cast<float>(h);
                            if (std::abs(camComp.aspectRatio - newAspect) > 0.001f) {
                                camComp.aspectRatio = newAspect;
                                camComp.updateProjectionMatrix();
                            }
                        }
                    }

                    // World-space eye position with the camera's LOCAL Euler rotation. Decomposing the
                    // world matrix to Euler (extractEulerAngleXYZ) has its gimbal singularity on the middle
                    // (yaw) axis at ±90°, which flips a yawing fixed-pitch camera to the sky (VK-1350).
                    const auto& localTransform = registry.get<components::TransformComponent>(entity);
                    camComp.updateViewMatrixFromWorldEye(worldTransform.worldMatrix, localTransform);
                    cameraPos = glm::vec3(worldTransform.worldMatrix[3]);
                } else {
                    cameraPos = glm::vec3(glm::inverse(camComp.viewMatrix)[3]);
                }

                events::render::UpdateMeshCameraCommand meshCameraCmd;
                meshCameraCmd.viewMatrix = camComp.viewMatrix;
                meshCameraCmd.projectionMatrix = camComp.projectionMatrix;
                meshCameraCmd.cameraPosition = cameraPos;
                meshCameraCmd.time = static_cast<float>(engineTime::Timer::getElapsedTime());
                events::EventDispatcher::instance().execute(meshCameraCmd);

                events::render::CameraPositionUpdatedNotification camPosNotif;
                camPosNotif.position = cameraPos;
                events::EventDispatcher::instance().publish(camPosNotif);

                events::render::UpdateIBLCameraCommand iblCameraCmd;
                iblCameraCmd.viewMatrix = camComp.viewMatrix;
                iblCameraCmd.projectionMatrix = camComp.projectionMatrix;
                events::EventDispatcher::instance().execute(iblCameraCmd);

                break;
            }

            if (renderService) {
                renderService->getViewportTexture();
            }
        }, threading::JobPriority::NORMAL, /*mainThread=*/true);

        auto renderFn = bootstrap->getRenderFn();
        frameTaskGraph->addTask("Render", [renderFn]() {
            if (renderFn) renderFn();
        });

        // Transforms depend on all service updates completing
        frameTaskGraph->addDependency("Transforms", "BehaviorTrees");
        frameTaskGraph->addDependency("Transforms", "Navmesh");
        frameTaskGraph->addDependency("Transforms", "AudioListener");
        frameTaskGraph->addDependency("Transforms", "WorldSector");
        frameTaskGraph->addDependency("Transforms", "AssetLifecycle");
        frameTaskGraph->addDependency("Transforms", "Plugins");

        // PostUpdate after transforms.
        frameTaskGraph->addDependency("PostUpdate", "Transforms");

        // Render after PostUpdate
        frameTaskGraph->addDependency("Render", "PostUpdate");

        if (frameTaskGraph->compile()) {
            frameTaskGraph->registerEventHandlers();
        }
    }

}
