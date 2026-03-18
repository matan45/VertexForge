#include "print/Log.hpp"
#include "EditorHandler.hpp"
#include "editor/EditorBootstrap.hpp"
#include "../splash/SplashScreen.hpp"
#include "impl/project/SceneServiceImpl.hpp"
#include "impl/render/EditorRenderServiceImpl.hpp"
#include "impl/input/InputServiceImpl.hpp"
#include "impl/input/ActionMappingServiceImpl.hpp"
#include "impl/editor/WindowStateServiceImpl.hpp"
#include "impl/render/PreviewServiceImpl.hpp"
#include "impl/editor/EditorModeServiceImpl.hpp"
#include "impl/audio/AudioServiceImpl.hpp"
#include "impl/scripting/ScriptingServiceImpl.hpp"
#include "impl/editor/UndoRedoServiceImpl.hpp"
#include "impl/project/FileOperationsServiceImpl.hpp"
#include "impl/physics/PhysicsServiceImpl.hpp"
#include "impl/physics/PhysicsAnimationServiceImpl.hpp"
#include "impl/navmesh/NavmeshServiceImpl.hpp"
#include "impl/physics/PhysicsPlayModeHandler.hpp"
#include "impl/vfx/VFXPlayModeHandler.hpp"
#include "impl/vfx/VFXRuntimeServiceImpl.hpp"
#include "impl/project/ProjectServiceImpl.hpp"
#include "impl/scene/TerrainService.hpp"
#include "impl/scene/WaterService.hpp"
#include "impl/editor/SculptModeServiceImpl.hpp"
#include "impl/terrain/BrushServiceImpl.hpp"
#include "impl/terrain/PaintModeServiceImpl.hpp"
#include "impl/terrain/PaintBrushServiceImpl.hpp"
#include "impl/terrain/HoleModeServiceImpl.hpp"
#include "impl/terrain/HoleBrushServiceImpl.hpp"
#include "impl/terrain/TerrainRaycastServiceImpl.hpp"
#include "impl/render/RenderTextureServiceImpl.hpp"
#include "impl/render/RenderTexturePlayModeHandler.hpp"
#include "impl/physics/ControllerServiceImpl.hpp"
#include "impl/render/RenderHookServiceImpl.hpp"
#include "impl/render/DebugDrawServiceImpl.hpp"
#include "impl/render/BillboardRenderServiceImpl.hpp"
#include "impl/render/DecalRenderServiceImpl.hpp"
#include "impl/render/LightStreamingServiceImpl.hpp"
#include "impl/render/ObjectStreamingServiceImpl.hpp"
#include "impl/render/GIServiceImpl.hpp"
#include "impl/ai/BehaviorTreeServiceImpl.hpp"
#include "impl/ai/BehaviorTreePlayModeHandler.hpp"
#include "impl/lifecycle/AssetLifecycleServiceImpl.hpp"
#include "impl/asset/AssetDatabaseServiceImpl.hpp"
#include "impl/world/WorldSectorServiceImpl.hpp"
#include "impl/vegetation/GrassServiceImpl.hpp"
#include "impl/vegetation/VegetationBrushServiceImpl.hpp"
#include "impl/vegetation/VegetationBrushModeServiceImpl.hpp"
#include "impl/meshbrush/MeshBrushModeServiceImpl.hpp"
#include "impl/meshbrush/MeshBrushServiceImpl.hpp"
#include "../adapters/terrain/TerrainRenderAdapter.hpp"
#include "../adapters/terrain/WaterRenderAdapter.hpp"
#include "../audio/AudioSceneUpdater.hpp"
#include "../audio/ReverbZoneManager.hpp"
#include "events/EventDispatcher.hpp"
#include "events/vegetation/GrassEvents.hpp"
#include "providers/vegetation/IGrassRenderProvider.hpp"
#include "time/Timer.hpp"
#include "events/project/ApplicationEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "Import.hpp"
#include "ExportHandler.hpp"
#include "core/PluginManager.hpp"
#include "resource/PathResolver.hpp"
#include <filesystem>

namespace handlers
{
    EditorHandler::EditorHandler()
        : bootstrap{std::make_unique<core::EditorBootstrap>()}
          , windowImguiHandler{std::make_unique<WindowImguiHandler>()}
    {
    }

    EditorHandler::~EditorHandler() = default;


    void EditorHandler::init()
    {
        resource::PathResolver::initialize();

        editor::SplashScreen::instance().setStatus("Initializing graphics...");
        bootstrap->init();

        editor::SplashScreen::instance().setStatus("Initializing import system...");
        controllers::Import::initialize();

        editor::SplashScreen::instance().setStatus("Registering services...");
        initializeServices();

        editor::SplashScreen::instance().setStatus("Loading plugins...");
        pluginManager = std::make_unique<plugin::PluginManager>(std::unordered_set<std::string>{
            std::string(plugin::capability::editor),
            std::string(plugin::capability::audio),
            std::string(plugin::capability::physics),
            std::string(plugin::capability::import_),
            std::string(plugin::capability::scripting),
            std::string(plugin::capability::graphics)
        });
        // Resolve plugins/ relative to the executable (bin/Editor/<Config>/x64/ -> repo root)
        auto exePath = std::filesystem::current_path();
        auto pluginsDir = exePath / "plugins";
        if (!std::filesystem::exists(pluginsDir)) {
            // When running from VS, working dir is project dir (VFEngine/editor/)
            pluginsDir = exePath / "../../plugins";
        }
        pluginManager->loadAll(pluginsDir);
        pluginManager->initializeAll();

        // Wire plugin-registered import stages into the import pipeline
        for (auto& stage : pluginManager->takeAllImportStages()) {
            controllers::Import::addCustomStage(std::move(stage));
        }

        bootstrap->setFrameCallback([this]()
        {
            // Process deferred scene loading before other updates
            if (sceneService)
            {
                sceneService->update();
            }

            if (inputService)
            {
                inputService->update();
            }
            if (windowStateService)
            {
                windowStateService->update();
            }

            if (editorModeService && editorModeService->isPlayMode())
            {
                float deltaTime = static_cast<float>(engineTime::Timer::getDeltaTime());

                // Update order is critical:
                // 1. Physics  - steps simulation and syncs transforms to ECS
                // 2. Scripts  - read input, set moveInput/jump/sprint on controllers
                // 3. Controller - applies movement from scripts via physics/navmesh/transform
                // 4. VFX      - updates particle simulations
                // 5. Audio    - uses final camera/listener positions

                if (physicsPlayModeHandler)
                {
                    physicsPlayModeHandler->update(deltaTime);
                }

                if (scriptingService)
                {
                    scriptingService->updateScripts(deltaTime);
                }

                if (controllerService)
                {
                    controllerService->applyControllerMovement(deltaTime);
                }

                if (behaviorTreeService)
                {
                    behaviorTreeService->updateAll(deltaTime);
                }

                if (vfxPlayModeHandler)
                {
                    vfxPlayModeHandler->update(deltaTime);
                }

                if (renderTexturePlayModeHandler)
                {
                    renderTexturePlayModeHandler->update(deltaTime);
                }

                if (audioSceneUpdater)
                {
                    audioSceneUpdater->updateListenerFromPrimaryCamera();
                }
            }

            // Update world sector streaming (distance-based entity load/unload)
            if (worldSectorService)
            {
                worldSectorService->update();
            }

            // Update asset lifecycle manager (deferred releases)
            if (assetLifecycleService)
            {
                float deltaTime = static_cast<float>(engineTime::Timer::getDeltaTime());
                assetLifecycleService->update(deltaTime);
            }

            // Update plugins every frame (regardless of play/edit mode)
            if (pluginManager)
            {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                pluginManager->updateAll(dt);
            }
        });

        bootstrap->setPostUpdateCallback([this]()
        {
            if (editorModeService && editorModeService->isPlayMode())
            {
                float deltaTime = static_cast<float>(engineTime::Timer::getDeltaTime());
                if (scriptingService)
                {
                    scriptingService->lateUpdateScripts(deltaTime);
                }
            }
        });

        // Wire script onFixedUpdate to run after each physics sub-step
        if (physicsPlayModeHandler && scriptingService)
        {
            physicsPlayModeHandler->setScriptFixedUpdateCallback([this](float fixedDt)
            {
                scriptingService->fixedUpdateScripts(fixedDt);
            });
        }

        setupEventSubscriptions();

        editor::SplashScreen::instance().setStatus("Setting up UI...");
        windowImguiHandler->init();
    }

    void EditorHandler::run() const
    {
        bootstrap->run();
    }

    void EditorHandler::cleanUp()
    {
        pluginManager.reset();
        exportHandler.reset();

        cleanupEventSubscriptions();

        windowImguiHandler->cleanUp();

        previewService.reset();
        renderService.reset();
        sceneService.reset();

        terrainService.reset();
        waterService.reset();
        projectService.reset();
        fileOperationsService.reset();
        undoRedoService.reset();
        physicsService.reset();
        physicsAnimationService.reset();
        navmeshService.reset();
        physicsPlayModeHandler.reset();
        vfxPlayModeHandler.reset();
        renderTexturePlayModeHandler.reset();
        vfxRuntimeService.reset();
        controllerService.reset();
        ikComponentService.reset();
        renderHookService.reset();
        debugDrawService.reset();
        audioSceneUpdater.reset();
        worldSectorService.reset();
        assetLifecycleService.reset();
        grassService.reset();
        vegetationBrushService.reset();
        vegetationBrushModeService.reset();
        meshBrushService.reset();
        meshBrushModeService.reset();
        lightStreamingService.reset();
        giService.reset();
        behaviorTreePlayModeHandler.reset();
        behaviorTreeService.reset();
        audioService.reset();
        scriptingService.reset();
        terrainRaycastService.reset();
        holeBrushService.reset();
        holeModeService.reset();
        paintBrushService.reset();
        paintModeService.reset();
        brushService.reset();
        sculptModeService.reset();
        editorModeService.reset();
        windowStateService.reset();
        actionMappingService.reset();
        inputService.reset();

        controllers::Import::shutdown();
        bootstrap->cleanUp();
    }

    bool EditorHandler::loadProject(const std::string& projectPath)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::project::LoadProjectCommand loadCmd;
        loadCmd.filePath = projectPath;
        if (!dispatcher.execute(loadCmd))
        {
            vfLogError("Failed to load project file: {}", projectPath);
            return false;
        }

        vfLogInfo("Project loaded from CLI: {}", projectPath);
        return true;
    }

    void EditorHandler::initializeServices()
    {
        createCoreServices();
        createMediaServices();
        createPhysicsServices();
        createVFXServices();
        createTerrainServices();
        createWaterServices();
        createVegetationServices();
        createMeshBrushServices();
        createAIServices();
        exportHandler = std::make_unique<handlers::ExportHandler>();
        registerAllEventHandlers();
    }

    void EditorHandler::createCoreServices()
    {
        sceneService = std::make_shared<services::SceneServiceImpl>(
            bootstrap->getSceneGraphSystem(),
            bootstrap->getAnimatorProvider(),
            bootstrap->getSocketProvider()
        );
        renderService = std::make_shared<services::EditorRenderServiceImpl>(
            bootstrap->getOffScreenProvider(),
            bootstrap->getEditorTextureProvider(),
            bootstrap->getPostProcessProvider()
        );
        inputService = std::make_shared<services::InputServiceImpl>(bootstrap->getWindow());
        actionMappingService = std::make_shared<services::ActionMappingServiceImpl>();
        windowStateService = std::make_shared<services::WindowStateServiceImpl>(bootstrap->getWindow());
        previewService = std::make_shared<services::PreviewServiceImpl>(
            bootstrap->getMaterialPreviewProvider(),
            bootstrap->getMeshPreviewProvider(),
            bootstrap->getAnimationPreviewProvider(),
            bootstrap->getVFXPreviewProvider()
        );
        editorModeService = std::make_shared<services::EditorModeServiceImpl>(bootstrap->getSceneGraphSystem());
        undoRedoService = std::make_shared<services::UndoRedoServiceImpl>();
        fileOperationsService = std::make_shared<services::FileOperationsServiceImpl>(undoRedoService);
        projectService = std::make_shared<services::ProjectServiceImpl>();
        assetDatabaseService = std::make_shared<services::AssetDatabaseServiceImpl>();
        renderTextureService = std::make_shared<services::RenderTextureServiceImpl>(
            bootstrap->getRenderTextureProvider()
        );

        if (auto* rttProvider = bootstrap->getRenderTextureProvider())
        {
            renderTexturePlayModeHandler = std::make_unique<services::RenderTexturePlayModeHandler>(rttProvider);
            renderTexturePlayModeHandler->subscribeToEvents();
        }

        renderHookService = std::make_shared<services::RenderHookServiceImpl>(
            bootstrap->getRenderHookProvider()
        );

        debugDrawService = std::make_shared<services::DebugDrawServiceImpl>(
            bootstrap->getDebugDrawProvider()
        );

        billboardRenderService = std::make_shared<services::BillboardRenderServiceImpl>(
            bootstrap->getBillboardRenderProvider()
        );

        decalRenderService = std::make_shared<services::DecalRenderServiceImpl>(
            bootstrap->getDecalRenderProvider()
        );

        assetLifecycleService = std::make_shared<services::AssetLifecycleServiceImpl>();

        lightStreamingService = std::make_shared<services::LightStreamingServiceImpl>(
            bootstrap->getLightStreamingProvider()
        );

        objectStreamingService = std::make_shared<services::ObjectStreamingServiceImpl>(
            bootstrap->getObjectStreamingProvider()
        );

        giService = std::make_shared<services::GIServiceImpl>(
            bootstrap->getGIProvider()
        );

        worldSectorService = std::make_shared<services::WorldSectorServiceImpl>(
            bootstrap->getSceneGraphSystem()
        );
    }

    void EditorHandler::createMediaServices()
    {
        audioService = std::make_shared<services::AudioServiceImpl>(bootstrap->getAudioProvider());
        scriptingService = std::make_shared<services::ScriptingServiceImpl>(
            bootstrap->getScriptingProvider(),
            bootstrap->getSceneGraphSystem()
        );
        audioSceneUpdater = std::make_unique<core::audio::AudioSceneUpdater>();
        auto* reverbZoneMgr = static_cast<core::audio::ReverbZoneManager*>(
            bootstrap->getAudioProvider()->getReverbZoneManager());
        audioSceneUpdater->setReverbZoneManager(reverbZoneMgr);
    }

    void EditorHandler::createPhysicsServices()
    {
        if (auto* physicsProvider = bootstrap->getPhysicsProvider())
        {
            physicsService = std::make_shared<services::PhysicsServiceImpl>(physicsProvider);
            physicsAnimationService = std::make_shared<services::PhysicsAnimationServiceImpl>(physicsProvider);
            physicsPlayModeHandler = std::make_unique<services::PhysicsPlayModeHandler>(physicsProvider);
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
    }

    void EditorHandler::createVFXServices()
    {
        if (auto* vfxProvider = bootstrap->getVFXRuntimeProvider())
        {
            vfxRuntimeService = std::make_unique<services::VFXRuntimeServiceImpl>(vfxProvider);
            vfxRuntimeService->registerEventHandlers();

            vfxPlayModeHandler = std::make_unique<services::VFXPlayModeHandler>(vfxProvider);
            vfxPlayModeHandler->subscribeToEvents();
        }
    }

    void EditorHandler::createTerrainServices()
    {
        auto terrainServiceImpl = std::make_shared<services::TerrainService>(bootstrap->getSceneGraphSystem());
        terrainService = terrainServiceImpl;

        if (auto* terrainAdapter = bootstrap->getTerrainRenderAdapterInternal())
        {
            terrainAdapter->setTerrainService(terrainServiceImpl.get());
        }

        terrainServiceImpl->setBrushComputeProvider(bootstrap->getTerrainBrushComputeProvider());

        if (auto* physicsProvider = bootstrap->getPhysicsProvider())
        {
            terrainServiceImpl->setPhysicsProvider(physicsProvider);
        }

        sculptModeService = std::make_shared<services::SculptModeServiceImpl>();
        brushService = std::make_shared<services::BrushServiceImpl>();
        paintModeService = std::make_shared<services::PaintModeServiceImpl>();
        paintBrushService = std::make_shared<services::PaintBrushServiceImpl>();
        holeModeService = std::make_shared<services::HoleModeServiceImpl>();
        holeBrushService = std::make_shared<services::HoleBrushServiceImpl>();
        terrainRaycastService = std::make_shared<services::TerrainRaycastServiceImpl>(
            bootstrap->getTerrainRaycastProvider());
    }

    void EditorHandler::createWaterServices()
    {
        auto waterServiceImpl = std::make_shared<services::WaterService>(bootstrap->getSceneGraphSystem());
        waterService = waterServiceImpl;

        waterServiceImpl->setPhysicsProvider(bootstrap->getPhysicsProvider());

        if (auto* waterAdapter = bootstrap->getWaterRenderAdapterInternal())
        {
            waterAdapter->setWaterService(waterServiceImpl.get());
        }

        if (physicsPlayModeHandler)
        {
            physicsPlayModeHandler->setWaterService(waterServiceImpl.get());
        }
    }

    void EditorHandler::createVegetationServices()
    {
        grassService = std::make_shared<services::GrassServiceImpl>();
        vegetationBrushService = std::make_shared<services::VegetationBrushServiceImpl>();
        vegetationBrushModeService = std::make_shared<services::VegetationBrushModeServiceImpl>();
        // Wire grass config callback so the adapter doesn't access EntityRegistry directly
        auto* grassProvider = bootstrap->getGrassRenderProvider();
        if (grassProvider)
        {
            grassProvider->setGetConfigCallback([]() {
                return events::EventDispatcher::instance().query(
                    events::vegetation::GetGlobalGrassConfigQuery{});
            });
        }
    }

    void EditorHandler::createMeshBrushServices()
    {
        meshBrushService = std::make_shared<services::MeshBrushServiceImpl>();
        meshBrushModeService = std::make_shared<services::MeshBrushModeServiceImpl>();
    }

    void EditorHandler::createAIServices()
    {
        behaviorTreeService = std::make_shared<services::BehaviorTreeServiceImpl>(
            bootstrap->getBehaviorTreeProvider()
        );

        if (auto* btProvider = bootstrap->getBehaviorTreeProvider())
        {
            behaviorTreePlayModeHandler = std::make_unique<services::BehaviorTreePlayModeHandler>(btProvider);
            behaviorTreePlayModeHandler->subscribeToEvents();
        }
    }

    void EditorHandler::registerAllEventHandlers()
    {
        sceneService->registerEventHandlers();
        renderService->registerEventHandlers();
        inputService->registerEventHandlers();
        actionMappingService->registerEventHandlers();
        windowStateService->registerEventHandlers();
        previewService->registerEventHandlers();
        editorModeService->registerEventHandlers();
        audioService->registerEventHandlers();
        scriptingService->registerEventHandlers();
        undoRedoService->registerEventHandlers();
        fileOperationsService->registerEventHandlers();
        physicsService->registerEventHandlers();
        if (physicsAnimationService)
        {
            physicsAnimationService->registerEventHandlers();
        }
        if (navmeshService)
        {
            navmeshService->registerEventHandlers();
        }
        projectService->registerEventHandlers();
        assetDatabaseService->registerEventHandlers();
        terrainService->registerEventHandlers();
        waterService->registerEventHandlers();
        sculptModeService->registerEventHandlers();
        brushService->registerEventHandlers();
        paintModeService->registerEventHandlers();
        paintBrushService->registerEventHandlers();
        holeModeService->registerEventHandlers();
        holeBrushService->registerEventHandlers();
        terrainRaycastService->registerEventHandlers();
        renderTextureService->registerEventHandlers();
        controllerService->registerEventHandlers();
        if (ikComponentService)
        {
            ikComponentService->registerEventHandlers();
        }
        exportHandler->registerEventHandlers();
        renderHookService->registerEventHandlers();
        debugDrawService->registerEventHandlers();
        assetLifecycleService->registerEventHandlers();
        worldSectorService->registerEventHandlers();
        grassService->registerEventHandlers();
        vegetationBrushService->registerEventHandlers();
        vegetationBrushModeService->registerEventHandlers();
        meshBrushService->registerEventHandlers();
        meshBrushModeService->registerEventHandlers();
        billboardRenderService->registerEventHandlers();
        decalRenderService->registerEventHandlers();
        lightStreamingService->registerEventHandlers();
        objectStreamingService->registerEventHandlers();
        giService->registerEventHandlers();
        behaviorTreeService->registerEventHandlers();

        events::render::LoadBillboardAtlasCommand atlasCmd;
        atlasCmd.atlasPath = resource::PathResolver::resolveEnginePath("../../resources/editor/billboardAtlas.vfImage");
        events::EventDispatcher::instance().execute(atlasCmd);
    }

    void EditorHandler::setupEventSubscriptions()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        resizeSubscription = dispatcher.subscribe<events::application::WindowResizedNotification>(
            [this](const events::application::WindowResizedNotification&)
            {
                bootstrap->triggerResize();
            });
    }

    void EditorHandler::cleanupEventSubscriptions()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (resizeSubscription.isValid())
        {
            dispatcher.unsubscribe(resizeSubscription);
            resizeSubscription = {};
        }
    }
}
