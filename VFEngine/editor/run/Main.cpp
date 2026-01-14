#include "../handlers/EditorHandler.hpp"
#include "../splash/SplashScreen.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
	editor::SplashScreen::instance().show();
	
	std::string projectPath;
	if (argc > 1) {
		projectPath = argv[1];
	}

	handlers::EditorHandler editorHandler;
	editorHandler.init();
	
	editor::SplashScreen::instance().close();
	
	if (!projectPath.empty()) {
		if (!editorHandler.loadProject(projectPath)) {
			std::cerr << "Failed to load project: " << projectPath << std::endl;
		}
	}

	editorHandler.run();
	editorHandler.cleanUp();
	return 0;
}