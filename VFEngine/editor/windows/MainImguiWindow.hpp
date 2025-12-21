#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "nfd/FileDialog.hpp"
#include "data/DTOs.hpp"
#include "events/EventDispatcher.hpp"

#include <filesystem>

namespace fs = std::filesystem;

namespace editor {
	class EditorCamera;
}

namespace windows
{
	class MainImguiWindow : public controllers::imguiHandler::ImguiWindow
	{
	private:
		int windowFlags;
		
		nfd::FileDialog fileDialog;
		std::vector<std::string> files;
		std::vector<bool> isFlip;
		bool openModal = false;
		
		bool showIBLWindow = false;
		fs::path selectedIBLFile;
		services::EditorTextureHandle iblPreviewHandle;
		
		bool showEditorCameraWindow = false;
		editor::EditorCamera* editorCameraRef = nullptr;  // Set by ViewPort
		
		bool showCullingStatsWindow = false;

		// Event subscription
		events::SubscriptionToken sceneClearedToken;

	public:
		explicit MainImguiWindow();
		~MainImguiWindow() override;

		void draw() override;
		
		void setEditorCamera(editor::EditorCamera* camera) { editorCameraRef = camera; }

	private:
		void menuBar();
		void importModel();

		void handleFileMenu();
		void handleSettingsMenu();
		void handleAddMenu();
		void handleDebug();
		void handlePlayControls();

		void iblWindow();
		void editorCameraWindow();
		void cullingStatsWindow();

		void subscribeToEvents();
		void onSceneCleared();
	};
}
