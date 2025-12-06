#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "nfd/FileDialog.hpp"
#include "interfaces/ISceneService.hpp"
#include "interfaces/IRenderService.hpp"
#include "interfaces/IResourceService.hpp"
#include "interfaces/IInputService.hpp"

// Legacy includes for backward compatibility
#include "CoreInterface.hpp"
#include "OffScreen.hpp"
#include "EditorTextureController.hpp"
#include "scene/SceneGraphSystem.hpp"

#include <filesystem>

namespace fs = std::filesystem;

namespace windows
{
	class MainImguiWindow : public controllers::imguiHandler::ImguiWindow
	{
	private:
		// Legacy references (kept for backward compatibility)
		controllers::CoreInterface& coreInterface;
		controllers::OffScreen& offscreen;
		std::shared_ptr<scene::SceneGraphSystem> sceneGraphSystem;

		int windowFlags;

		// Import settings
		nfd::FileDialog fileDialog;
		std::vector<std::string> files;
		std::vector<bool> isFlip;
		bool openModal = false;

		// IBL window
		fs::path selectedIBLFile;
		bool showIBLWindow = false;
		bool deletePreview = false;
		dto::EditorTexture* iblPreview{ nullptr };

		// Service-based IBL preview handle
		services::EditorTextureHandle iblPreviewHandle;

		// Flag to use services
		bool useServices = false;

	public:
		explicit MainImguiWindow(controllers::CoreInterface& coreInterface, controllers::OffScreen& offscreen, std::shared_ptr<scene::SceneGraphSystem> sceneGraphSystem);
		~MainImguiWindow() override;

		void draw() override;

		// Enable service-based mode
		void enableServiceMode() { useServices = true; }

	private:
		void menuBar();
		void importModel();

		void handleFileMenu();
		void handleSettingsMenu();
		void handleAddMenu();

		void iblWindow();

		// Service-based methods
		void importModelWithServices();
		void iblWindowWithServices();

		components::CameraComponent* getFirstCameraComponent() const;
	};
}
