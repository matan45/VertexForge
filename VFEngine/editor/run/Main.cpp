#include "../handlers/EditorHandler.hpp"
#include "../splash/SplashScreen.hpp"
#include "crash/CrashHandler.hpp"
#include "print/Log.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
	// Install the crash handler first (so even a fault during log init is
	// dumped), then mirror the log to disk.
	util::installCrashHandler();
	util::initLogFile("Editor");

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