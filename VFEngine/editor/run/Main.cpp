#include "../handlers/EditorHandler.hpp"
#include "../splash/SplashScreen.hpp"
#include "crash/CrashHandler.hpp"
#include "print/Log.hpp"
#include <iostream>
#include <string>
#include <string_view>

namespace
{
	// Editor.exe [projectPath] [--mcp] [--mcp-port <n>] [--mcp-token <t>]
	// The first non-flag argument is the project path; flags may appear in any order.
	void parseArguments(int argc, char* argv[], std::string& projectPath, editor::McpLaunchOptions& mcpOptions)
	{
		for (int i = 1; i < argc; ++i) {
			const std::string_view arg = argv[i];

			if (arg == "--mcp") {
				mcpOptions.enable = true;
			}
			else if (arg == "--mcp-port") {
				if (i + 1 >= argc) {
					vfLogWarning("--mcp-port needs a value; ignored");
					continue;
				}
				const std::string_view value = argv[++i];
				if (auto port = editor::EditorMcpHost::parsePort(value)) {
					mcpOptions.port = *port;
				}
				else {
					vfLogWarning("--mcp-port {}: not a port in {}-{}; ignored", value,
						editor::EditorMcpHost::minPort, editor::EditorMcpHost::maxPort);
				}
			}
			else if (arg == "--mcp-token") {
				if (i + 1 >= argc) {
					vfLogWarning("--mcp-token needs a value; ignored");
					continue;
				}
				mcpOptions.token = std::string(argv[++i]);
			}
			else if (arg.starts_with("--")) {
				vfLogWarning("Unknown argument {}; ignored", arg);
			}
			else if (projectPath.empty()) {
				projectPath = std::string(arg);
			}
			else {
				vfLogWarning("Extra argument {}; ignored", arg);
			}
		}
	}
}

int main(int argc, char* argv[]) {
	// Install the crash handler first (so even a fault during log init is
	// dumped), then mirror the log to disk.
	util::installCrashHandler();
	util::initLogFile("Editor");

	editor::SplashScreen::instance().show();

	std::string projectPath;
	editor::McpLaunchOptions mcpOptions;
	parseArguments(argc, argv, projectPath, mcpOptions);

	handlers::EditorHandler editorHandler;
	editorHandler.setMcpLaunchOptions(mcpOptions);
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
