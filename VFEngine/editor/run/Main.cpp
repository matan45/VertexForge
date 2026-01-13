#include "../handlers/EditorHandler.hpp"
#include "../splash/SplashScreen.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
	// Show splash screen immediately (before any heavy init)
	editor::SplashScreen::instance().show();

	// Parse CLI: Editor.exe [project_path]
	std::string projectPath;
	if (argc > 1) {
		projectPath = argv[1];
	}

	handlers::EditorHandler editorHandler;
	editorHandler.init();

	// Close splash when initialization is complete
	editor::SplashScreen::instance().close();

	// Load project if path provided via CLI
	if (!projectPath.empty()) {
		if (!editorHandler.loadProject(projectPath)) {
			std::cerr << "Failed to load project: " << projectPath << std::endl;
		}
	}

	editorHandler.run();
	editorHandler.cleanUp();
	return 0;
}