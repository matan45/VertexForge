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

		// Import settings
		nfd::FileDialog fileDialog;
		std::vector<std::string> files;
		std::vector<bool> isFlip;
		bool openModal = false;

		// IBL window
		fs::path selectedIBLFile;
		bool showIBLWindow = false;
		services::EditorTextureHandle iblPreviewHandle;

		// Editor Camera settings window
		bool showEditorCameraWindow = false;
		editor::EditorCamera* editorCameraRef = nullptr;  // Set by ViewPort

		// Culling stats debug window
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

		void iblWindow();
		void editorCameraWindow();
		void cullingStatsWindow();

		void subscribeToEvents();
		void onSceneCleared();
	};
}
