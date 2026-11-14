#include "print/Log.hpp"
#include "EditorHandler.hpp"
#include "ExportHandler.hpp"
#include "HeightmapGenerationHandler.hpp"
#include "PluginComponentHandler.hpp"
#include "PluginMcpToolHandler.hpp"
#include "PluginPostProcessEffectHandler.hpp"
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
#include "impl/ai/BehaviorTreeAuthoringService.hpp"
#include "impl/ai/EQSAuthoringService.hpp"
#include "impl/vfx/VFXRuntimeServiceImpl.hpp"
#include "impl/vfx/VFXSequencePlayModeHandler.hpp"
#include "impl/vfx/VFXSequenceRuntimeServiceImpl.hpp"
#include "impl/vfx/VFXEditPreviewHandler.hpp"
#include "impl/vfx/VFXAssetSummaryService.hpp"
#include "impl/render/EditorRenderServiceImpl.hpp"
#include "../../core/audio/AudioSceneUpdater.hpp"
#include "impl/threading/FrameTaskGraph.hpp"
#include "impl/common/VfsBridge.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/project/ApplicationEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "events/scene/SceneSettingsEvents.hpp"
#include "Import.hpp"
#include "core/PluginManager.hpp"
#include "launch/Utf8Path.hpp"
#include "resource/PathResolver.hpp"
#include "time/Timer.hpp"
#include <algorithm>
#include <exception>
#include <filesystem>
#include <mutex>
#include <system_error>

namespace handlers
{
    namespace
    {
        // VK-1682: what the scene notifications report while the startup watch is armed. They can
        // be published from the Scene task on a worker thread. Subscribers hold this by shared_ptr,
        // never `this`: publish() calls a copy of the subscriber list outside its lock, so a handler
        // can still run after unsubscribe() returned.
        struct StartupSignals
        {
            std::atomic<bool> sceneLoaded{false};
            std::atomic<int> lastPercent{-1};
            std::mutex failureMutex;
            std::string failure;
        };
    }

    struct EditorHandler::StartupWatch
    {
        std::shared_ptr<StartupSignals> signals = std::make_shared<StartupSignals>();
        std::string warning;
        StartupCallback onDone;
        // Declared last: destroyed (unsubscribed) first.
        events::ScopedSubscription loadedSubscription;
        events::ScopedSubscription completedSubscription;
        events::ScopedSubscription progressSubscription;
    };

    EditorHandler::EditorHandler()
        : bootstrap{std::make_unique<core::EditorBootstrap>()}
          , windowImguiHandler{std::make_unique<WindowImguiHandler>()}
    {
    }

    EditorHandler::~EditorHandler() = default;

    void EditorHandler::init()
    {
        resource::PathResolver::initialize();
        services::configureVfsBridges();

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
            // VK-1650 / VK-1653: MCP tool calls run FIRST, on the main thread, with no graph
            // task in flight AND the render thread idle. MainLoop::run called
            // renderController->beginFrame() before this callback, which waits for the
            // previous frame's render to finish, and the render thread is not woken again
            // until this frame's "Render" task calls FrameSynchronizer::endFrame(). After
            // execute() it is NOT idle: the render thread is then walking the scene and the
            // terrain grids (getRawVisibleTiles streams tiles, regenerates meshes and drains
            // runtime terrain edits), so a tool that mutated EnTT or a terrain from there
            // raced it. Corollary: a Main-affinity tool must never wait for a rendered frame
            // -- nothing renders until this callback returns.
            if (mcpHost)
            {
                mcpHost->drain();
            }

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

            // VK-1682: also here, after execute(): the startup scene loads inside this frame's Scene
            // task, so by now it has finished (or failed) and no task touches the watch.
            pollStartupWatch();
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
        // Created before the UI so the status bar and preferences window can hold it;
        // started after, once every service and plugin the tools dispatch to is up.
        mcpHost = std::make_unique<editor::EditorMcpHost>();
        windowImguiHandler->init(mcpHost.get());

        editor::SplashScreen::instance().setStatus("Starting MCP server...");
        mcpHost->init(mcpLaunchOptions);
    }

    void EditorHandler::run() const
    {
        bootstrap->run();
    }

    void EditorHandler::cleanUp()
    {
        // VK-1650: first, so no tool call runs against services being torn down.
        // Pending calls fail with "shutting down".
        if (mcpHost)
        {
            mcpHost->shutdown();
        }

        bootstrap->stopRenderThread();

        if (frameTaskGraph) {
            frameTaskGraph->unregisterEventHandlers();
            frameTaskGraph.reset();
        }

        // Import shuts down before the plugin DLLs unload: plugin-registered
        // importers (and the pipeline that may reference them) must be torn
        // down while their vtables still exist.
        controllers::Import::shutdown();

        pluginComponentHandler.reset();
        pluginMcpToolHandler.reset();
        pluginPostProcessEffectHandler.reset();
        pluginManager.reset();
        exportHandler.reset();
        heightmapGenerationHandler.reset(); // waits for a running heightmap job
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
        volumetricNavService.reset();
        physicsPlayModeHandler.reset();
        oceanService.reset();
        vfxPlayModeHandler.reset();
        renderTexturePlayModeHandler.reset();
        // VK-1657: before the runtime service whose vfxruntime handlers the preview dispatches to.
        vfxEditPreviewHandler.reset();
        vfxAssetSummaryService.reset();
        vfxRuntimeService.reset();
        controllerService.reset();
        ikComponentService.reset();
        renderHookService.reset();
        customPipelineService.reset();
        pluginTextureService.reset();
        debugDrawService.reset();
        reverbCameraSubscription.unsubscribe();  // VK-1505: drop reverb sub before its target dies.
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
        behaviorTreeAuthoringService.reset(); // stateless; its saves dispatch to the BT service below
        eqsAuthoringService.reset(); // stateless; its saves notify the EQS service below
        behaviorTreePlayModeHandler.reset();
        behaviorTreeService.reset();
        eqsService.reset(); // after the BT service: stopping trees cancels EQS queries
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

    EditorHandler::ProjectLoadResult EditorHandler::loadProject(const std::string& projectPath)
    {
        ProjectLoadResult result;
        auto& dispatcher = events::EventDispatcher::instance();

        editor::SplashScreen::instance().setStatus(
            "Loading project " + launch::pathToUtf8(std::filesystem::path(projectPath).filename()) + "...");

        // Synchronous: parses the .vfproj and rebuilds the asset database before it returns.
        events::project::LoadProjectCommand loadCmd;
        loadCmd.filePath = projectPath;
        if (!dispatcher.execute(loadCmd))
        {
            vfLogError("Failed to load project file: {}", projectPath);
            return result;
        }
        result.loaded = true;

        vfLogInfo("Project loaded from CLI: {}", projectPath);

        // Queue the startup scene from the project config. LoadSceneCommand only queues it: the
        // load runs in the first frame's Scene task (watchStartup reports when it is done).
        auto projectOpt = dispatcher.query(events::project::GetCurrentProjectQuery{});
        if (projectOpt.has_value() && !projectOpt->startupScene.empty())
        {
            std::filesystem::path scenePath =
                std::filesystem::path(projectOpt->workingDirectory) / projectOpt->startupScene;

            std::error_code ec;
            if (std::filesystem::exists(scenePath, ec))
            {
                editor::SplashScreen::instance().setStatus(
                    "Loading scene " + launch::pathToUtf8(scenePath.filename()) + "...");

                events::scene::LoadSceneCommand sceneCmd;
                sceneCmd.filePath = scenePath.string();
                result.sceneQueued = dispatcher.execute(sceneCmd);
                if (result.sceneQueued)
                {
                    vfLogInfo("Startup scene queued: {}", scenePath.string());
                }
                else
                {
                    result.warning = "its startup scene " + launch::pathToUtf8(scenePath) + " could not be queued";
                    vfLogWarning("Startup scene could not be queued: {}", scenePath.string());
                }
            }
            else
            {
                result.warning = "its startup scene " + launch::pathToUtf8(scenePath) + " was not found";
                vfLogWarning("Startup scene not found: {}", scenePath.string());
            }
        }

        return result;
    }

    void EditorHandler::watchStartup(std::string warning, StartupCallback onDone)
    {
        auto watch = std::make_unique<StartupWatch>();
        watch->warning = std::move(warning);
        watch->onDone = std::move(onDone);

        auto& dispatcher = events::EventDispatcher::instance();
        const std::shared_ptr<StartupSignals> signals = watch->signals;

        // Any scene, not only the startup one: an MCP call drained at the top of the first frame
        // may already have replaced the queued load.
        watch->loadedSubscription = events::ScopedSubscription(
            dispatcher.subscribe<events::scene::SceneLoadedNotification>(
                [signals](const events::scene::SceneLoadedNotification&)
                {
                    signals->sceneLoaded.store(true);
                }));

        watch->completedSubscription = events::ScopedSubscription(
            dispatcher.subscribe<events::scene::SceneLoadingCompletedNotification>(
                [signals](const events::scene::SceneLoadingCompletedNotification& notification)
                {
                    if (!notification.success)
                    {
                        std::lock_guard<std::mutex> lock(signals->failureMutex);
                        signals->failure = notification.errorMessage.empty() ? std::string("the scene file failed to load")
                                                                             : notification.errorMessage;
                    }
                }));

        // The splash repaints only when its status changes, so a percentage keeps it visibly alive.
        watch->progressSubscription = events::ScopedSubscription(
            dispatcher.subscribe<events::scene::SceneLoadingProgressUpdatedNotification>(
                [signals](const events::scene::SceneLoadingProgressUpdatedNotification& notification)
                {
                    if (!(notification.progress >= 0.0f))
                    {
                        return;
                    }
                    const int percent = std::min(100, static_cast<int>(notification.progress * 100.0f));
                    if (signals->lastPercent.exchange(percent) != percent)
                    {
                        editor::SplashScreen::instance().setStatus("Loading scene... " + std::to_string(percent) + "%");
                    }
                }));

        startupWatch = std::move(watch);
    }

    void EditorHandler::pollStartupWatch()
    {
        if (!startupWatch)
        {
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();
        try
        {
            // Also false when the queued load was replaced or never queued: the watch never hangs.
            if (dispatcher.query(events::scene::IsSceneLoadPendingQuery{}))
            {
                return;
            }
        }
        catch (const std::exception&)
        {
            // No scene service to ask: nothing can be pending.
        }

        std::unique_ptr<StartupWatch> watch = std::move(startupWatch);
        watch->loadedSubscription.unsubscribe();
        watch->completedSubscription.unsubscribe();
        watch->progressSubscription.unsubscribe();

        // The project may have changed since loadProject (an MCP project_open in the first frame).
        std::string projectName;
        try
        {
            if (auto project = dispatcher.query(events::project::GetCurrentProjectQuery{}))
            {
                projectName = project->projectName;
            }
        }
        catch (const std::exception&)
        {
        }

        std::string failure;
        {
            std::lock_guard<std::mutex> lock(watch->signals->failureMutex);
            failure = watch->signals->failure;
        }

        StartupOutcome outcome;
        outcome.sceneLoaded = watch->signals->sceneLoaded.load();
        const std::string subject = projectName.empty() ? std::string("The project") : "Project " + projectName;
        if (outcome.sceneLoaded)
        {
            outcome.message = subject + " is ready";
        }
        else if (!watch->warning.empty())
        {
            outcome.message = subject + " is open, but " + watch->warning;
        }
        else if (!failure.empty())
        {
            outcome.message = subject + " is open, but its startup scene failed to load: " + failure;
        }
        else
        {
            outcome.message = subject + " is open, but its startup scene did not finish loading";
        }

        if (!outcome.sceneLoaded)
        {
            vfLogWarning("{}", outcome.message);
        }

        editor::SplashScreen::instance().close();

        if (watch->onDone)
        {
            try
            {
                watch->onDone(outcome);
            }
            catch (const std::exception& e)
            {
                vfLogError("Startup callback failed: {}", e.what());
            }
        }
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

        // VK-1682: an editor closed during its startup load never resolves the watch.
        startupWatch.reset();

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
