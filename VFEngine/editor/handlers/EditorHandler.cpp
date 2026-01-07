#include "EditorHandler.hpp"
#include "EditorBootstrap.hpp"
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
#include "../audio/AudioSceneUpdater.hpp"
#include "events/EventDispatcher.hpp"
#include "time/Timer.hpp"
#include "events/ApplicationEvents.hpp"
#include "events/RenderEvents.hpp"
#include "Import.hpp"

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
        bootstrap->init();

        controllers::Import::initialize();

        initializeServices();

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
                if (scriptingService)
                {
                    float deltaTime = static_cast<float>(engineTime::Timer::getDeltaTime());
                    scriptingService->updateScripts(deltaTime);
                }

                if (audioSceneUpdater)
                {
                    audioSceneUpdater->updateListenerFromPrimaryCamera();
                }
            }
        });
        
        setupEventSubscriptions();

        windowImguiHandler->init();
    }

    void EditorHandler::run() const
    {
        bootstrap->run();
    }

    void EditorHandler::cleanUp()
    {
        cleanupEventSubscriptions();

        windowImguiHandler->cleanUp();

        previewService.reset();
        renderService.reset();
        sceneService.reset();

        fileOperationsService.reset();
        undoRedoService.reset();
        audioSceneUpdater.reset();
        audioService.reset();
        scriptingService.reset();
        editorModeService.reset();
        windowStateService.reset();
        inputService.reset();

        bootstrap->cleanUp();
    }

    void EditorHandler::initializeServices()
    {
        // Create service implementations using providers from bootstrap
        sceneService = std::make_shared<services::SceneServiceImpl>(bootstrap->getSceneGraphSystem());
        renderService = std::make_shared<services::EditorRenderServiceImpl>(
            bootstrap->getOffScreenProvider(),
            bootstrap->getEditorTextureProvider()
        );
        inputService = std::make_shared<services::InputServiceImpl>(bootstrap->getWindow());
        windowStateService = std::make_shared<services::WindowStateServiceImpl>(bootstrap->getWindow());
        previewService = std::make_shared<services::PreviewServiceImpl>(
            bootstrap->getMaterialPreviewProvider(),
            bootstrap->getMeshPreviewProvider()
        );
        editorModeService = std::make_shared<services::EditorModeServiceImpl>(bootstrap->getSceneGraphSystem());
        audioService = std::make_shared<services::AudioServiceImpl>(bootstrap->getAudioProvider());
        scriptingService = std::make_shared<services::ScriptingServiceImpl>(
            bootstrap->getScriptingProvider(),
            bootstrap->getSceneGraphSystem()
        );
        audioSceneUpdater = std::make_unique<core::audio::AudioSceneUpdater>();

        // Create undo/redo service (standalone, no providers needed)
        undoRedoService = std::make_shared<services::UndoRedoServiceImpl>();

        // Create file operations service with undo/redo integration
        fileOperationsService = std::make_shared<services::FileOperationsServiceImpl>(undoRedoService);

        // Register event handlers for command/query pattern
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
