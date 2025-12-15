#include "EditorHandler.hpp"
#include "EditorBootstrap.hpp"
#include "impl/SceneServiceImpl.hpp"
#include "impl/RenderServiceImpl.hpp"
#include "impl/InputServiceImpl.hpp"
#include "impl/ResourceServiceImpl.hpp"
#include "impl/PreviewServiceImpl.hpp"
#include "Import.hpp"
#include "config/Config.hpp"
#include "print/EditorLogger.hpp"

namespace handlers {
	EditorHandler::EditorHandler()
		: bootstrap{ std::make_unique<core::EditorBootstrap>() }
		, windowImguiHandler{ std::make_unique<WindowImguiHandler>() }
	{
	}

	EditorHandler::~EditorHandler() = default;


	void EditorHandler::init()
	{
		// Initialize core systems via bootstrap
		bootstrap->init();

		// Initialize services with providers from bootstrap
		initializeServices();

		windowImguiHandler->init();
	}

	void EditorHandler::run() const
	{
		bootstrap->run();
	}

	void EditorHandler::cleanUp()
	{
		windowImguiHandler->cleanUp();

		// Reset services before graphics cleanup to release Vulkan resources
		previewService.reset();
		renderService.reset();
		sceneService.reset();
		inputService.reset();
		resourceService.reset();

		// Clean up via bootstrap
		bootstrap->cleanUp();
	}

	void EditorHandler::initializeServices()
	{
		// Get shared instances from the bootstrap
		auto sceneGraphSystem = bootstrap->getSceneGraphSystem();

		// Create service implementations using providers from bootstrap
		auto sceneServiceImpl = std::make_shared<services::SceneServiceImpl>(sceneGraphSystem);
		auto renderServiceImpl = std::make_shared<services::RenderServiceImpl>(
			bootstrap->getOffScreenProvider(),
			bootstrap->getEditorTextureProvider()
		);
		auto inputServiceImpl = std::make_shared<services::InputServiceImpl>(bootstrap->getWindow());
		auto previewServiceImpl = std::make_shared<services::PreviewServiceImpl>(
			bootstrap->getPreviewProvider()
		);

		sceneService = sceneServiceImpl;
		renderService = renderServiceImpl;
		inputService = inputServiceImpl;
		previewService = previewServiceImpl;

		// Register event handlers for command/query pattern
		sceneServiceImpl->registerEventHandlers();
		renderServiceImpl->registerEventHandlers();
		inputServiceImpl->registerEventHandlers();
		previewServiceImpl->registerEventHandlers();

		// Create resource service with import delegate
		auto resourceServiceImpl = std::make_shared<services::ResourceServiceImpl>();

		// Set up import delegate to bridge to Import controller
		services::ImportDelegate importDelegate;
		importDelegate.initialize = []() {
			controllers::Import::initialize();
		};
		importDelegate.importFiles = [](const std::vector<services::ImportFileRequest>& files,
		                                services::ImportProgressCallback progressCallback) -> services::ImportResultData {
			std::vector<importConfig::ImportFiles> importFiles;
			for (const auto& file : files) {
				importConfig::ImportConfig config;
				config.isImageFlipVertically = file.flipVertically;
				importFiles.emplace_back(file.path, config);
			}
			// Convert progress callback to Import's callback type
			controllers::ImportProgressCallback importProgressCallback = nullptr;
			if (progressCallback) {
				importProgressCallback = [progressCallback](std::string_view currentFile,
				                                             uint32_t fileIndex,
				                                             uint32_t totalFiles,
				                                             float fileProgress) {
					progressCallback(currentFile, fileIndex, totalFiles, fileProgress);
				};
			}
			auto controllerResult = controllers::Import::importFiles(importFiles, importProgressCallback);

			// Convert controller result to service result
			services::ImportResultData result;
			result.successCount = controllerResult.successCount;
			result.failureCount = controllerResult.failureCount;
			for (const auto& fileResult : controllerResult.fileResults) {
				services::ImportFileResultData serviceFileResult;
				serviceFileResult.sourcePath = fileResult.sourcePath;
				serviceFileResult.fileName = fileResult.fileName;
				serviceFileResult.success = fileResult.success;
				serviceFileResult.errorMessage = fileResult.errorMessage;
				result.fileResults.push_back(std::move(serviceFileResult));
			}
			return result;
		};
		importDelegate.setLocation = [](const std::string& path) {
			controllers::Import::setLocation(path);
		};

		resourceServiceImpl->setImportDelegate(importDelegate);
		resourceServiceImpl->registerEventHandlers();
		resourceService = resourceServiceImpl;
	}
}
