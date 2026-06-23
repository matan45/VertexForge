#include "print/Log.hpp"
#include "EditorHandler.hpp"
#include "ExportHandler.hpp"
#include "impl/save/SaveService.hpp"
#include "impl/save/ConfigService.hpp"
#include "impl/editor/EditorSettingsService.hpp"
#include "impl/editor/EditorKeybindingServiceImpl.hpp"
#include "editor/EditorBootstrap.hpp"
#include "../splash/SplashScreen.hpp"
#include "impl/physics/PhysicsPlayModeHandler.hpp"
#include "impl/vfx/VFXPlayModeHandler.hpp"
#include "impl/render/RenderTexturePlayModeHandler.hpp"
#include "impl/ai/BehaviorTreePlayModeHandler.hpp"
#include "impl/vfx/VFXRuntimeServiceImpl.hpp"
#include "impl/vfx/VFXSequencePlayModeHandler.hpp"
#include "impl/vfx/VFXSequenceRuntimeServiceImpl.hpp"
#include "impl/render/EditorRenderServiceImpl.hpp"
#include "../../core/audio/AudioSceneUpdater.hpp"
#include "impl/threading/FrameTaskGraph.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/project/ApplicationEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "Import.hpp"
#include "core/PluginManager.hpp"
#include "resource/PathResolver.hpp"
#include "time/Timer.hpp"
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
            std::string(plugin::capability::graphics),
            std::string(plugin::capability::terrain),
            std::string(plugin::capability::input),
            std::string(plugin::capability::navmesh),
            std::string(plugin::capability::vfx)
        });
        pluginManager->loadAll(plugin::PluginManager::resolvePluginsDirectory());
        pluginManager->initializeAll();

        for (auto& stage : pluginManager->takeAllImportStages()) {
            controllers::Import::addCustomStage(std::move(stage));
        }

        for (auto& [importerPluginName, importer] : pluginManager->takeAllAssetImporters()) {
            controllers::Import::registerImporter(std::move(importer), importerPluginName);
        }

        buildFrameTaskGraph();

        bootstrap->setFrameCallback([this]()
        {
            frameTaskGraph->execute();

            // Phase 4c: execute() blocks until every task (including the worker that
            // ran the physics post-step / fixedUpdateScripts) has joined, so here we
            // are back on the main thread with no graph task in flight — the safe
            // point to apply a script-fatal-triggered Stop. Dispatching the mode
            // change here (rather than from inside the off-thread callback) avoids
            // re-entrancy and off-main-thread scene mutation.
            if (playFatalErrorRequested.exchange(false, std::memory_order_relaxed) &&
                editorModeService && editorModeService->isPlayMode())
            {
                events::editor::SetEditorModeCommand stopCmd;
                stopCmd.mode = services::EditorMode::Edit;
                events::EventDispatcher::instance().execute(stopCmd);
            }
        });

        editorRenderServiceImpl = dynamic_cast<services::EditorRenderServiceImpl*>(renderService.get());
        bootstrap->setPreRenderCallback([this]()
        {
            if (editorRenderServiceImpl)
            {
                editorRenderServiceImpl->renderViewportDeferred([this]()
                {
                    if (editorModeService && editorModeService->isPlayMode() &&
                        !editorModeService->isPaused() && renderTexturePlayModeHandler)
                    {
                        float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                        renderTexturePlayModeHandler->update(dt);
                    }
                });
            }
        });

        if (physicsPlayModeHandler && scriptingService)
        {
            physicsPlayModeHandler->setScriptFixedUpdateCallback([this](float fixedDt)
            {
                // Phase 4c: this is the one script path not already guarded by the
                // frame task graph's per-task try/catch — it runs from the physics
                // post-step callback (PhysicsSync task, which may execute off the main
                // thread). Catch interpreter-level errors so a bad script can't crash
                // the editor, and request a safe Stop (applied next on the main thread
                // after the frame completes, never dispatched from here). SEH/JIT
                // faults remain uncatchable and go to the crash handler.
                try
                {
                    scriptingService->fixedUpdateScripts(fixedDt);
                }
                catch (const std::exception& e)
                {
                    vfLogError("[Script] fixedUpdateScripts threw: {} — stopping play mode", e.what());
                    playFatalErrorRequested.store(true, std::memory_order_relaxed);
                }
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
        bootstrap->stopRenderThread();

        if (frameTaskGraph) {
            frameTaskGraph->unregisterEventHandlers();
            frameTaskGraph.reset();
        }

        // Import shuts down before the plugin DLLs unload: plugin-registered
        // importers (and the pipeline that may reference them) must be torn
        // down while their vtables still exist.
        controllers::Import::shutdown();

        pluginManager.reset();
        exportHandler.reset();
        cleanupEventSubscriptions();
        windowImguiHandler->cleanUp();

        previewService.reset();
        renderService.reset();
        sceneService.reset();
        terrainService.reset();
        projectService.reset();
        fileOperationsService.reset();
        undoRedoService.reset();
        physicsService.reset();
        physicsAnimationService.reset();
        navmeshService.reset();
        physicsPlayModeHandler.reset();
        oceanService.reset();
        vfxPlayModeHandler.reset();
        renderTexturePlayModeHandler.reset();
        vfxRuntimeService.reset();
        controllerService.reset();
        ikComponentService.reset();
        renderHookService.reset();
        customPipelineService.reset();
        pluginTextureService.reset();
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

        // Load the startup scene from the project config
        auto projectOpt = dispatcher.query(events::project::GetCurrentProjectQuery{});
        if (projectOpt.has_value() && !projectOpt->startupScene.empty())
        {
            std::filesystem::path scenePath =
                std::filesystem::path(projectOpt->workingDirectory) / projectOpt->startupScene;

            if (std::filesystem::exists(scenePath))
            {
                events::scene::LoadSceneCommand sceneCmd;
                sceneCmd.filePath = scenePath.string();
                dispatcher.execute(sceneCmd);
                vfLogInfo("Startup scene loaded: {}", scenePath.string());
            }
            else
            {
                vfLogWarning("Startup scene not found: {}", scenePath.string());
            }
        }

        return true;
    }

    void EditorHandler::setupEventSubscriptions()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        resizeSubscription = dispatcher.subscribe<events::application::WindowResizedNotification>(
            [this](const events::application::WindowResizedNotification&)
            {
                bootstrap->triggerResize();
            });

        displaySettingsSubscription = dispatcher.subscribe<events::application::ApplyDisplaySettingsNotification>(
            [this](const events::application::ApplyDisplaySettingsNotification& n)
            {
                bootstrap->applyDisplaySettings(n.presentMode, n.msaa);
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

        if (displaySettingsSubscription.isValid())
        {
            dispatcher.unsubscribe(displaySettingsSubscription);
            displaySettingsSubscription = {};
        }
    }
}
