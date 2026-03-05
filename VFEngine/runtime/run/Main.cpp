#include "../handlers/RuntimeHandler.hpp"
#include "print/Log.hpp"
#include "print/RuntimeDebugLog.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>

int main(int argc, char* argv[])
{
    // Redirect crash/error info to a log file next to the executable
    std::ofstream crashLog("runtime_crash.log", std::ios::trunc);

    // Set up global debug log callback so all modules can write to crash log
    util::runtimeDebugLogFn = [&crashLog](const std::string& msg)
    {
        crashLog << msg << std::endl;
        crashLog.flush();
    };

    // Disable info/warning logs for shipped games
    util::loggingEnabled = true;

    util::runtimeDebugLog("Creating RuntimeHandler...");
    handlers::RuntimeHandler runtime;
    util::runtimeDebugLog("RuntimeHandler created.");

    try
    {
        util::runtimeDebugLog("Starting runtime.init()...");

        runtime.init();
        util::runtimeDebugLog("Init complete.");

        std::string projectPath;
        if (argc > 1)
        {
            projectPath = argv[1];
        }
        else
        {
            // Auto-discover .vfproj next to the executable
            for (const auto& entry : std::filesystem::directory_iterator("."))
            {
                if (entry.path().extension() == ".vfproj")
                {
                    projectPath = entry.path().string();
                    break;
                }
            }
        }

        util::runtimeDebugLog("Project path: " + (projectPath.empty() ? std::string("(none)") : projectPath));

        if (!projectPath.empty())
        {
            if (!runtime.loadProject(projectPath))
            {
                util::runtimeDebugLog("Failed to load project: " + projectPath);
            }
            else
            {
                util::runtimeDebugLog("Project loaded successfully.");
            }
        }

        util::runtimeDebugLog("Entering main loop...");

        runtime.run();
        runtime.cleanUp();

        util::runtimeDebugLog("Clean shutdown.");
    }
    catch (const std::exception& e)
    {
        util::runtimeDebugLog(std::string("FATAL: ") + e.what());
        return 1;
    }

    return 0;
}
