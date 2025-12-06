#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "nfd/FileDialog.hpp"
#include "interfaces/ISceneService.hpp"
#include "interfaces/IRenderService.hpp"
#include "interfaces/IResourceService.hpp"
#include "interfaces/IInputService.hpp"

#include <filesystem>

namespace fs = std::filesystem;

namespace windows
{
	class MainImguiWindow : public controllers::imguiHandler::ImguiWindow
	{
	private:
		int windowFlags;

		// Import settings
		nfd::FileDialog fileDialog;
		std::vector<std::string> files;
		std::vector<bool> isFlip;
		bool openModal = false;

		// IBL window
		fs::path selectedIBLFile;
		bool showIBLWindow = false;
		services::EditorTextureHandle iblPreviewHandle;

	public:
		MainImguiWindow();
		~MainImguiWindow() override = default;

		void draw() override;

	private:
		void menuBar();
		void importModel();

		void handleFileMenu();
		void handleSettingsMenu();
		void handleAddMenu();

		void iblWindow();
	};
}
