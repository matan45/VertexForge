#include "../handlers/RuntimeHandler.hpp"
#include "print/Log.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>

int main(int argc, char* argv[])
{
    // Redirect crash/error info to a log file next to the executable
    std::ofstream crashLog("runtime_crash.log", std::ios::trunc);

    // Disable info/warning logs for shipped games
    util::loggingEnabled = false;

    handlers::RuntimeHandler runtime;

    try
    {
        crashLog << "Starting runtime..." << std::endl;

        runtime.init();
        crashLog << "Init complete." << std::endl;

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

        crashLog << "Project path: " << (projectPath.empty() ? "(none)" : projectPath) << std::endl;

        if (!projectPath.empty())
        {
            if (!runtime.loadProject(projectPath))
            {
                crashLog << "Failed to load project: " << projectPath << std::endl;
            }
            else
            {
                crashLog << "Project loaded successfully." << std::endl;
            }
        }

        crashLog << "Entering main loop..." << std::endl;
        crashLog.flush();

        runtime.run();
        runtime.cleanUp();

        crashLog << "Clean shutdown." << std::endl;
    }
    catch (const std::exception& e)
    {
        crashLog << "FATAL: " << e.what() << std::endl;
        crashLog.flush();
        return 1;
    }

    return 0;
}
