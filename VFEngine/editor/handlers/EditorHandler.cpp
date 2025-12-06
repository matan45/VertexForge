#include "EditorHandler.hpp"
#include "impl/SceneServiceImpl.hpp"
#include "impl/RenderServiceImpl.hpp"
#include "impl/InputServiceImpl.hpp"
#include "impl/ResourceServiceImpl.hpp"
#include "scene/LevelHandler.hpp"
#include "Import.hpp"
#include "config/Config.hpp"
#include <stdexcept>
#include "print/EditorLogger.hpp"

namespace handlers {
	EditorHandler::EditorHandler()
		: coreInterface{ std::make_unique<controllers::CoreInterface>() }
		, offScreenInterface{ std::make_unique<controllers::OffScreen>() }
		, windowImguiHandler{ std::make_unique<WindowImguiHandler>() }
	{
	}

	EditorHandler::~EditorHandler() = default;

	void EditorHandler::verifyPhase(InitPhase required, const char* operation) const
	{
		if (currentPhase < required) {
			vfLogError("EditorHandler: Cannot {} - initialization phase {} required, current is {}",
				operation, static_cast<int>(required), static_cast<int>(currentPhase));
			throw std::runtime_error(std::string("EditorHandler initialization order violation: ") + operation);
		}
	}

	void EditorHandler::init()
	{
		// Phase 1: Initialize Core (Vulkan context, window, graphics)
		// This must complete before any services can be created
		coreInterface->init();
		currentPhase = InitPhase::CoreInitialized;

		// Phase 2: Initialize services after core is ready
		// Services depend on: Window (for input), OffScreen (for render), LevelHandler (for scene)
		initializeServices();
		currentPhase = InitPhase::ServicesInitialized;

		// Phase 3: Initialize ImGui windows
		// Windows use EventDispatcher for cross-layer communication
		windowImguiHandler->init();
		currentPhase = InitPhase::WindowsInitialized;

		// Phase 4: Initialize offscreen rendering
		offScreenInterface->init();
		currentPhase = InitPhase::OffScreenInitialized;

		// Mark fully initialized
		currentPhase = InitPhase::FullyInitialized;
	}

	void EditorHandler::run() const
	{
		verifyPhase(InitPhase::FullyInitialized, "run");
		coreInterface->run();
	}

	void EditorHandler::cleanUp()
	{
		windowImguiHandler->cleanUp();

		// Reset services before graphics cleanup to release Vulkan resources
		renderService.reset();
		sceneService.reset();
		inputService.reset();
		resourceService.reset();

		offScreenInterface->cleanUp();
		coreInterface->cleanUp();
	}

	void EditorHandler::initializeServices()
	{
		// Verify core is initialized before creating services
		verifyPhase(InitPhase::CoreInitialized, "initializeServices");

		// Get shared instances from the level/core
		auto level = scene::LevelHandler::getInstance();
		auto sceneGraphSystem = level->getSceneGraphSystem();

		// Create input controller (Core layer wrapping Window)
		auto* windowPtr = coreInterface->getWindow();
		inputController = std::make_unique<controllers::InputController>(windowPtr);

		// Create service implementations
		auto sceneServiceImpl = std::make_shared<services::SceneServiceImpl>(sceneGraphSystem);
		auto renderServiceImpl = std::make_shared<services::RenderServiceImpl>(offScreenInterface.get());

		sceneService = sceneServiceImpl;
		renderService = renderServiceImpl;
		auto inputServiceImpl = std::make_shared<services::InputServiceImpl>(inputController.get());
		inputService = inputServiceImpl;

		// Register event handlers for command/query pattern
		sceneServiceImpl->registerEventHandlers();
		renderServiceImpl->registerEventHandlers();
		inputServiceImpl->registerEventHandlers();

		// Create resource service with import delegate
		auto resourceServiceImpl = std::make_shared<services::ResourceServiceImpl>();

		// Set up import delegate to bridge to Import controller
		services::ImportDelegate importDelegate;
		importDelegate.initialize = []() {
			controllers::Import::initialize();
		};
		importDelegate.importFiles = [](const std::vector<services::ImportFileRequest>& files) {
			std::vector<importConfig::ImportFiles> importFiles;
			for (const auto& file : files) {
				importConfig::ImportConfig config;
				config.isImageFlipVertically = file.flipVertically;
				importFiles.emplace_back(file.path, config);
			}
			controllers::Import::importFiles(importFiles);
		};
		importDelegate.setLocation = [](const std::string& path) {
			controllers::Import::setLocation(path);
		};

		resourceServiceImpl->setImportDelegate(importDelegate);
		resourceServiceImpl->registerEventHandlers();
		resourceService = resourceServiceImpl;
	}
}
