#include "EditorHandler.hpp"
#include "impl/SceneServiceImpl.hpp"
#include "impl/RenderServiceImpl.hpp"
#include "impl/InputServiceImpl.hpp"
#include "impl/ResourceServiceImpl.hpp"
#include "scene/LevelHandler.hpp"
#include "Import.hpp"
#include "config/Config.hpp"
#include "print/EditorLogger.hpp"

namespace handlers {
	EditorHandler::EditorHandler()
		: coreInterface{ std::make_unique<controllers::CoreInterface>() }
		, offScreenInterface{ std::make_unique<controllers::OffScreen>() }
		, windowImguiHandler{ std::make_unique<WindowImguiHandler>() }
	{
	}

	EditorHandler::~EditorHandler() = default;
	

	void EditorHandler::init()
	{
		
		coreInterface->init();
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
		// Get shared instances from the level/core
		auto level = scene::LevelHandler::getInstance();
		auto sceneGraphSystem = level->getSceneGraphSystem();

		// Create service implementations
		auto sceneServiceImpl = std::make_shared<services::SceneServiceImpl>(sceneGraphSystem);
		auto renderServiceImpl = std::make_shared<services::RenderServiceImpl>(offScreenInterface.get());
		auto inputServiceImpl = std::make_shared<services::InputServiceImpl>(coreInterface->getWindow());

		sceneService = sceneServiceImpl;
		renderService = renderServiceImpl;
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
