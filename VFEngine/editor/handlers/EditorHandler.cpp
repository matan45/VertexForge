#include "EditorHandler.hpp"
#include "ServiceLocator.hpp"
#include "impl/SceneServiceImpl.hpp"
#include "impl/RenderServiceImpl.hpp"
#include "impl/InputServiceImpl.hpp"
#include "impl/ResourceServiceImpl.hpp"
#include "scene/LevelHandler.hpp"
#include "Import.hpp"
#include "config/Config.hpp"

namespace handlers {
	EditorHandler::EditorHandler() :coreInterface{ std::make_unique<controllers::CoreInterface>() },
		offScreenInterface{ new controllers::OffScreen() },
		windowImguiHandler{ std::make_unique<WindowImguiHandler>() }
	{

	}

	EditorHandler::~EditorHandler()
	{
		delete offScreenInterface;
	}

	void EditorHandler::init()
	{
		//need also to load the level here
		coreInterface->init();

		// Initialize services after core is ready (but before windows)
		initializeServices();

		windowImguiHandler->init();

		offScreenInterface->init();
	}

	void EditorHandler::run() const
	{
		coreInterface->run();
	}

	void EditorHandler::cleanUp()
	{
		windowImguiHandler->cleanUp();

		// Clear services before graphics cleanup to release Vulkan resources
		services::ServiceLocator::instance().clear();
		renderService.reset();
		sceneService.reset();
		inputService.reset();
		resourceService.reset();

		offScreenInterface->cleanUp();
		coreInterface->cleanUp();
	}

	void EditorHandler::initializeServices()
	{
		// Get shared instances from the level/core
		auto level = scene::LevelHandler::getInstance();
		auto sceneGraphSystem = level->getSceneGraphSystem();

		// Get window pointer for input service
		auto* windowPtr = coreInterface->getWindow();

		// Create service implementations
		sceneService = std::make_shared<services::SceneServiceImpl>(sceneGraphSystem);
		renderService = std::make_shared<services::RenderServiceImpl>(offScreenInterface);
		inputService = std::make_shared<services::InputServiceImpl>(windowPtr);

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
		resourceService = resourceServiceImpl;

		// Register with ServiceLocator
		auto& locator = services::ServiceLocator::instance();
		locator.registerService<services::ISceneService>(sceneService);
		locator.registerService<services::IRenderService>(renderService);
		locator.registerService<services::IInputService>(inputService);
		locator.registerService<services::IResourceService>(resourceService);
	}
}
