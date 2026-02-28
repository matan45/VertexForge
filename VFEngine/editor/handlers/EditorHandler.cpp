#include "EditorHandler.hpp"
#include "EditorBootstrap.hpp"
#include "../splash/SplashScreen.hpp"
#include "impl/SceneServiceImpl.hpp"
#include "impl/EditorRenderServiceImpl.hpp"
#include "impl/InputServiceImpl.hpp"
#include "impl/WindowStateServiceImpl.hpp"
#include "impl/PreviewServiceImpl.hpp"
#include "impl/EditorModeServiceImpl.hpp"
#include "impl/AudioServiceImpl.hpp"
#include "impl/ScriptingServiceImpl.hpp"
#include "impl/UndoRedoServiceImpl.hpp"
#include "impl/FileOperationsServiceImpl.hpp"
#include "impl/PhysicsServiceImpl.hpp"
#include "impl/PhysicsAnimationServiceImpl.hpp"
#include "impl/NavmeshServiceImpl.hpp"
#include "impl/PhysicsPlayModeHandler.hpp"
#include "impl/VFXPlayModeHandler.hpp"
#include "impl/VFXRuntimeServiceImpl.hpp"
#include "impl/ProjectServiceImpl.hpp"
#include "impl/scene/TerrainService.hpp"
#include "impl/scene/WaterService.hpp"
#include "impl/SculptModeServiceImpl.hpp"
#include "impl/BrushServiceImpl.hpp"
#include "impl/PaintModeServiceImpl.hpp"
#include "impl/PaintBrushServiceImpl.hpp"
#include "impl/TerrainRaycastServiceImpl.hpp"
#include "impl/RenderTextureServiceImpl.hpp"
#include "impl/RenderTexturePlayModeHandler.hpp"
#include "impl/LightBakeServiceImpl.hpp"
#include "../adapters/TerrainRenderAdapter.hpp"
#include "../adapters/WaterRenderAdapter.hpp"
#include "../audio/AudioSceneUpdater.hpp"
#include "events/EventDispatcher.hpp"
#include "time/Timer.hpp"
#include "events/ApplicationEvents.hpp"
#include "events/RenderEvents.hpp"
#include "events/ProjectEvents.hpp"
#include "print/EditorLogger.hpp"
#include "Import.hpp"
#include "core/PluginManager.hpp"
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
            std::string(plugin::capability::scripting)
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
                // 1. Physics - steps simulation and syncs transforms to ECS
                // 2. Scripts - can read updated transforms and apply game logic
                // 3. VFX     - updates particle simulations
                // 4. Audio   - uses final camera/listener positions

                if (physicsPlayModeHandler)
                {
                    physicsPlayModeHandler->update(deltaTime);
                }

                if (scriptingService)
                {
                    scriptingService->updateScripts(deltaTime);
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

            // Update plugins every frame (regardless of play/edit mode)
            if (pluginManager)
            {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                pluginManager->updateAll(dt);
            }
        });

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
        lightBakeService.reset();
        audioSceneUpdater.reset();
        audioService.reset();
        scriptingService.reset();
        terrainRaycastService.reset();
        paintBrushService.reset();
        paintModeService.reset();
        brushService.reset();
        sculptModeService.reset();
        editorModeService.reset();
        windowStateService.reset();
        inputService.reset();

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
        renderTextureService = std::make_shared<services::RenderTextureServiceImpl>(
            bootstrap->getRenderTextureProvider()
        );

        if (auto* rttProvider = bootstrap->getRenderTextureProvider())
        {
            renderTexturePlayModeHandler = std::make_unique<services::RenderTexturePlayModeHandler>(rttProvider);
            renderTexturePlayModeHandler->subscribeToEvents();
        }

        lightBakeService = std::make_shared<services::LightBakeServiceImpl>(
            bootstrap->getLightBakeProvider()
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

    void EditorHandler::registerAllEventHandlers()
    {
        sceneService->registerEventHandlers();
        renderService->registerEventHandlers();
        inputService->registerEventHandlers();
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
        terrainService->registerEventHandlers();
        waterService->registerEventHandlers();
        sculptModeService->registerEventHandlers();
        brushService->registerEventHandlers();
        paintModeService->registerEventHandlers();
        paintBrushService->registerEventHandlers();
        terrainRaycastService->registerEventHandlers();
        renderTextureService->registerEventHandlers();
        lightBakeService->registerEventHandlers();

        events::render::LoadBillboardAtlasCommand atlasCmd;
        atlasCmd.atlasPath = "../../resources/editor/billboardAtlas.vfImage";
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
