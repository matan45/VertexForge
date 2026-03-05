#include "print/Log.hpp"
#include "RuntimeHandler.hpp"
#include "runtime/RuntimeBootstrap.hpp"
#include "impl/project/SceneServiceImpl.hpp"
#include "impl/render/RuntimeRenderServiceImpl.hpp"
#include "impl/input/InputServiceImpl.hpp"
#include "impl/editor/WindowStateServiceImpl.hpp"
#include "impl/audio/AudioServiceImpl.hpp"
#include "impl/scripting/ScriptingServiceImpl.hpp"
#include "impl/project/ProjectServiceImpl.hpp"
#include "impl/scene/TerrainService.hpp"
#include "impl/scene/WaterService.hpp"
#include "impl/physics/PhysicsServiceImpl.hpp"
#include "impl/physics/PhysicsAnimationServiceImpl.hpp"
#include "impl/navmesh/NavmeshServiceImpl.hpp"
#include "impl/physics/PhysicsPlayModeHandler.hpp"
#include "impl/physics/ControllerServiceImpl.hpp"
#include "../audio/AudioSceneUpdater.hpp"
#include "../adapters/terrain/TerrainRenderAdapter.hpp"
#include "../adapters/terrain/WaterRenderAdapter.hpp"
#include "impl/render/RenderTextureServiceImpl.hpp"
#include "impl/render/RenderTexturePlayModeHandler.hpp"
#include "impl/render/DebugDrawServiceImpl.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ApplicationEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/project/SceneEvents.hpp"
#include "resource/PathResolver.hpp"
#include <filesystem>
#include "time/Timer.hpp"
#include "core/PluginManager.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/CoreComponents.hpp"
#include "events/render/RenderEvents.hpp"
#include "math/TransformUtils.hpp"

#include "print/RuntimeDebugLog.hpp"

namespace handlers {

    RuntimeHandler::RuntimeHandler()
        : bootstrap(std::make_unique<core::RuntimeBootstrap>()) {}

    RuntimeHandler::~RuntimeHandler() = default;

    void RuntimeHandler::init() {
        util::runtimeDebugLog("  PathResolver::initialize()...");
        resource::PathResolver::initialize();
        util::runtimeDebugLog("  PathResolver done.");

        util::runtimeDebugLog("  bootstrap->init()...");
        bootstrap->init();
        util::runtimeDebugLog("  bootstrap->init() done.");

        util::runtimeDebugLog("  initializeServices()...");
        initializeServices();
        util::runtimeDebugLog("  initializeServices() done.");

        util::runtimeDebugLog("  Creating PluginManager...");
        pluginManager = std::make_unique<plugin::PluginManager>(std::unordered_set<std::string>{
            std::string(plugin::capability::audio),
            std::string(plugin::capability::physics),
            std::string(plugin::capability::scripting)
        });
        auto exePath = std::filesystem::current_path();
        auto pluginsDir = exePath / "plugins";
        if (!std::filesystem::exists(pluginsDir)) {
            pluginsDir = exePath / "../../plugins";
        }
        util::runtimeDebugLog("  Loading plugins from: " + pluginsDir.string());
        pluginManager->loadAll(pluginsDir);
        pluginManager->initializeAll();
        util::runtimeDebugLog("  Plugins loaded.");

        bootstrap->setFrameCallback([this]() {
            // Process deferred scene loading before other updates
            if (sceneService) {
                sceneService->update();
            }

            if (inputService) {
                inputService->update();
            }
            if (windowStateService) {
                windowStateService->update();
            }

            float deltaTime = static_cast<float>(engineTime::Timer::getDeltaTime());

            // Update order: Physics → Scripts → Controller Movement → Audio
            if (physicsPlayModeHandler) {
                physicsPlayModeHandler->update(deltaTime);
            }

            if (scriptingService) {
                scriptingService->updateScripts(deltaTime);
            }

            if (controllerService) {
                controllerService->applyControllerMovement(deltaTime);
            }

            if (renderTexturePlayModeHandler) {
                renderTexturePlayModeHandler->update(deltaTime);
            }

            if (audioSceneUpdater) {
                audioSceneUpdater->updateListenerFromPrimaryCamera();
            }

            if (pluginManager) {
                pluginManager->updateAll(deltaTime);
            }
        });

        // Post-update callback runs AFTER scene graph update (WorldTransformComponent is valid)
        bootstrap->setPostUpdateCallback([this]() {
            // Push primary camera matrices to the render system each frame
            {
                static int camLogCount = 0;
                auto& registry = scene::EntityRegistry::getRegistry();
                auto cameraView = registry.view<components::CameraComponent, components::WorldTransformComponent>();

                bool foundPrimary = false;
                int totalCameras = 0;
                for (auto entity : cameraView) {
                    totalCameras++;
                    auto& camComp = cameraView.get<components::CameraComponent>(entity);
                    if (!camComp.isPrimary) continue;
                    foundPrimary = true;

                    auto& worldTransform = cameraView.get<components::WorldTransformComponent>(entity);

                    // Update aspect ratio to match window
                    if (windowStateService) {
                        uint32_t w = windowStateService->getWidth();
                        uint32_t h = windowStateService->getHeight();
                        if (camLogCount < 5) {
                            util::runtimeDebugLog("  Window size: " + std::to_string(w) + "x" + std::to_string(h));
                        }
                        if (w > 0 && h > 0) {
                            float newAspect = static_cast<float>(w) / static_cast<float>(h);
                            if (std::abs(camComp.aspectRatio - newAspect) > 0.001f) {
                                camComp.aspectRatio = newAspect;
                                camComp.updateProjectionMatrix();
                            }
                        }
                    }

                    // Decompose world matrix into position/rotation (same as editor play mode)
                    auto decomposed = math::decomposeMatrix(worldTransform.worldMatrix);
                    camComp.updateViewMatrix(decomposed.position, decomposed.rotation);

                    // Extract camera position from decomposed transform
                    glm::vec3 cameraPos = decomposed.position;

                    if (camLogCount < 5) {
                        util::runtimeDebugLog("  Camera found: pos=(" + std::to_string(cameraPos.x) + "," + std::to_string(cameraPos.y) + "," + std::to_string(cameraPos.z) + ") fov=" + std::to_string(camComp.fieldOfView) + " aspect=" + std::to_string(camComp.aspectRatio));
                        // Log view matrix
                        const auto& v = camComp.viewMatrix;
                        util::runtimeDebugLog("  ViewMatrix row0=(" + std::to_string(v[0][0]) + "," + std::to_string(v[1][0]) + "," + std::to_string(v[2][0]) + "," + std::to_string(v[3][0]) + ")");
                        util::runtimeDebugLog("  ViewMatrix row1=(" + std::to_string(v[0][1]) + "," + std::to_string(v[1][1]) + "," + std::to_string(v[2][1]) + "," + std::to_string(v[3][1]) + ")");
                        util::runtimeDebugLog("  ViewMatrix row2=(" + std::to_string(v[0][2]) + "," + std::to_string(v[1][2]) + "," + std::to_string(v[2][2]) + "," + std::to_string(v[3][2]) + ")");
                        util::runtimeDebugLog("  ViewMatrix row3=(" + std::to_string(v[0][3]) + "," + std::to_string(v[1][3]) + "," + std::to_string(v[2][3]) + "," + std::to_string(v[3][3]) + ")");
                        // Log projection matrix
                        const auto& p = camComp.projectionMatrix;
                        util::runtimeDebugLog("  ProjMatrix row0=(" + std::to_string(p[0][0]) + "," + std::to_string(p[1][0]) + "," + std::to_string(p[2][0]) + "," + std::to_string(p[3][0]) + ")");
                        util::runtimeDebugLog("  ProjMatrix row1=(" + std::to_string(p[0][1]) + "," + std::to_string(p[1][1]) + "," + std::to_string(p[2][1]) + "," + std::to_string(p[3][1]) + ")");
                        util::runtimeDebugLog("  ProjMatrix row2=(" + std::to_string(p[0][2]) + "," + std::to_string(p[1][2]) + "," + std::to_string(p[2][2]) + "," + std::to_string(p[3][2]) + ")");
                        util::runtimeDebugLog("  ProjMatrix row3=(" + std::to_string(p[0][3]) + "," + std::to_string(p[1][3]) + "," + std::to_string(p[2][3]) + "," + std::to_string(p[3][3]) + ")");
                        // Log world matrix
                        const auto& wm = worldTransform.worldMatrix;
                        util::runtimeDebugLog("  WorldMatrix row0=(" + std::to_string(wm[0][0]) + "," + std::to_string(wm[1][0]) + "," + std::to_string(wm[2][0]) + "," + std::to_string(wm[3][0]) + ")");
                        util::runtimeDebugLog("  WorldMatrix row1=(" + std::to_string(wm[0][1]) + "," + std::to_string(wm[1][1]) + "," + std::to_string(wm[2][1]) + "," + std::to_string(wm[3][1]) + ")");
                        util::runtimeDebugLog("  WorldMatrix row2=(" + std::to_string(wm[0][2]) + "," + std::to_string(wm[1][2]) + "," + std::to_string(wm[2][2]) + "," + std::to_string(wm[3][2]) + ")");
                        util::runtimeDebugLog("  WorldMatrix row3=(" + std::to_string(wm[0][3]) + "," + std::to_string(wm[1][3]) + "," + std::to_string(wm[2][3]) + "," + std::to_string(wm[3][3]) + ")");
                    }

                    // Push to render system
                    events::render::UpdateMeshCameraCommand meshCameraCmd;
                    meshCameraCmd.viewMatrix = camComp.viewMatrix;
                    meshCameraCmd.projectionMatrix = camComp.projectionMatrix;
                    meshCameraCmd.cameraPosition = cameraPos;
                    meshCameraCmd.time = static_cast<float>(engineTime::Timer::getElapsedTime());
                    events::EventDispatcher::instance().execute(meshCameraCmd);

                    // Push camera to IBL skybox renderer
                    events::render::UpdateIBLCameraCommand iblCameraCmd;
                    iblCameraCmd.viewMatrix = camComp.viewMatrix;
                    iblCameraCmd.projectionMatrix = camComp.projectionMatrix;
                    events::EventDispatcher::instance().execute(iblCameraCmd);

                    break; // Only use the first primary camera
                }

                if (camLogCount < 5) {
                    util::runtimeDebugLog("  Camera query: total=" + std::to_string(totalCameras) + " foundPrimary=" + std::to_string(foundPrimary));
                    camLogCount++;
                }
            }

            // Trigger offscreen scene render (prepares cameras, meshes, then renders)
            if (renderService) {
                renderService->getViewportTexture();
            }
        });
        
        setupEventSubscriptions();
    }

    void RuntimeHandler::run() const {
        bootstrap->run();
    }

    void RuntimeHandler::cleanUp() {
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
        controllerService.reset();
        ikComponentService.reset();
        physicsAnimationService.reset();
        physicsService.reset();
        navmeshService.reset();
        waterService.reset();
        terrainService.reset();
        projectService.reset();
        audioSceneUpdater.reset();
        audioService.reset();
        scriptingService.reset();
        renderTextureService.reset();
        debugDrawService.reset();
        renderService.reset();
        sceneService.reset();
        windowStateService.reset();
        inputService.reset();

        bootstrap->cleanUp();
    }

    bool RuntimeHandler::loadProject(const std::string& projectPath) {
        util::runtimeDebugLog("  loadProject: " + projectPath);
        auto& dispatcher = events::EventDispatcher::instance();

        events::project::LoadProjectCommand loadCmd;
        loadCmd.filePath = projectPath;
        util::runtimeDebugLog("  loadProject: executing LoadProjectCommand...");
        if (!dispatcher.execute(loadCmd)) {
            util::runtimeDebugLog("  loadProject: FAILED to load project file");
            vfLogError("Failed to load project file: {}", projectPath);
            dispatcher.execute(events::scene::NewSceneCommand{});
            return false;
        }
        util::runtimeDebugLog("  loadProject: project file loaded");

        auto projectOpt = dispatcher.query(events::project::GetCurrentProjectQuery{});
        if (!projectOpt) {
            util::runtimeDebugLog("  loadProject: FAILED to get project configuration");
            vfLogError("Failed to get project configuration");
            dispatcher.execute(events::scene::NewSceneCommand{});
            return false;
        }

        util::runtimeDebugLog("  loadProject: projectName=" + projectOpt->projectName);
        util::runtimeDebugLog("  loadProject: workingDirectory=" + projectOpt->workingDirectory);
        util::runtimeDebugLog("  loadProject: startupScene=" + projectOpt->startupScene);

        // Set window title from project config
        bootstrap->setWindowTitle(projectOpt->projectName);

        // Try to load a window icon (.vfImage) from the assets folder (optional)
        if (!projectOpt->exeIconPath.empty())
        {
            // Derive a .vfImage path from the icon path for GLFW window icon
            std::filesystem::path iconBase =
                std::filesystem::path(projectOpt->workingDirectory) / projectOpt->exeIconPath;
            util::runtimeDebugLog("  loadProject: trying icon at " + iconBase.string());
            // Try the path as-is first (user may have set a .vfImage directly)
            if (std::filesystem::exists(iconBase))
            {
                bootstrap->setWindowIcon(iconBase.string());
            }
            else
            {
                // Try with .vfImage extension (in case the config stores an .ico path)
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
        util::runtimeDebugLog("  loadProject: scenePath=" + scenePath.string());

        if (!std::filesystem::exists(scenePath)) {
            util::runtimeDebugLog("  loadProject: startup scene NOT FOUND: " + scenePath.string());
            vfLogError("Startup scene not found: {}", scenePath.string());
            dispatcher.execute(events::scene::NewSceneCommand{});
            return false;
        }

        util::runtimeDebugLog("  loadProject: loading scene...");
        events::scene::LoadSceneCommand sceneCmd;
        sceneCmd.filePath = scenePath.string();
        if (!dispatcher.execute(sceneCmd)) {
            util::runtimeDebugLog("  loadProject: FAILED to queue scene load");
            vfLogError("Failed to queue startup scene load: {}", scenePath.string());
            dispatcher.execute(events::scene::NewSceneCommand{});
            return false;
        }
        util::runtimeDebugLog("  loadProject: scene load queued successfully");

        // PhysicsPlayModeHandler listens for EditorModeChangedNotification
        if (physicsPlayModeHandler)
        {
            util::runtimeDebugLog("  loadProject: entering play mode (physics)...");
            events::editor::EditorModeChangedNotification notification;
            notification.previousMode = services::EditorMode::Edit;
            notification.currentMode = services::EditorMode::Play;
            dispatcher.publish(notification);
        }

        util::runtimeDebugLog("  loadProject: done");
        return true;
    }

    void RuntimeHandler::initializeServices() {
        sceneService = std::make_shared<services::SceneServiceImpl>(
            bootstrap->getSceneGraphSystem(),
            bootstrap->getAnimatorProvider(),
            bootstrap->getSocketProvider()
        );
        renderService = std::make_shared<services::RuntimeRenderServiceImpl>(
            bootstrap->getOffScreenProvider(),
            bootstrap->getPostProcessProvider()
        );
        inputService = std::make_shared<services::InputServiceImpl>(bootstrap->getWindow());
        windowStateService = std::make_shared<services::WindowStateServiceImpl>(bootstrap->getWindow());
        audioService = std::make_shared<services::AudioServiceImpl>(bootstrap->getAudioProvider());
        audioSceneUpdater = std::make_unique<core::audio::AudioSceneUpdater>();

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

        auto waterServiceImpl = std::make_shared<services::WaterService>(bootstrap->getSceneGraphSystem());
        waterService = waterServiceImpl;
        waterServiceImpl->setPhysicsProvider(bootstrap->getPhysicsProvider());
        if (auto* waterAdapter = bootstrap->getWaterRenderAdapterInternal())
        {
            waterAdapter->setWaterService(waterServiceImpl.get());
        }

        if (auto* physicsProvider = bootstrap->getPhysicsProvider())
        {
            physicsService = std::make_shared<services::PhysicsServiceImpl>(physicsProvider);
            physicsAnimationService = std::make_shared<services::PhysicsAnimationServiceImpl>(physicsProvider);
            physicsPlayModeHandler = std::make_unique<services::PhysicsPlayModeHandler>(physicsProvider);
            physicsPlayModeHandler->setWaterService(waterServiceImpl.get());
            physicsPlayModeHandler->subscribeToEvents();
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

        if (auto* rttProvider = bootstrap->getRenderTextureProvider())
        {
            renderTexturePlayModeHandler = std::make_unique<services::RenderTexturePlayModeHandler>(rttProvider);
            renderTexturePlayModeHandler->subscribeToEvents();
        }

        sceneService->registerEventHandlers();
        projectService->registerEventHandlers();
        renderService->registerEventHandlers();
        inputService->registerEventHandlers();
        windowStateService->registerEventHandlers();
        static_cast<services::AudioServiceImpl*>(audioService.get())->registerEventHandlers();
        static_cast<services::ScriptingServiceImpl*>(scriptingService.get())->registerEventHandlers();
        terrainService->registerEventHandlers();
        waterService->registerEventHandlers();
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
        controllerService->registerEventHandlers();
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
    }

    void RuntimeHandler::cleanupEventSubscriptions()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (resizeSubscription.isValid()) {
            dispatcher.unsubscribe(resizeSubscription);
            resizeSubscription = {};
        }
    }

}
