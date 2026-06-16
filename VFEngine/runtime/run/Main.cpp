#include "../handlers/RuntimeHandler.hpp"
#include "crash/CrashHandler.hpp"
#include "print/Log.hpp"
#include <iostream>
#include <filesystem>


int main(int argc, char* argv[])
{
    // Install the crash handler first (so even a fault during log init is
    // dumped), then mirror the log to disk.
    util::installCrashHandler();
    util::initLogFile("Runtime");

    handlers::RuntimeHandler runtime;

    try
    {
        runtime.init();

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

        if (!projectPath.empty())
        {
            if (!runtime.loadProject(projectPath))
            {
                std::cerr << "Failed to load project: " << projectPath << std::endl;
            }
        }

        runtime.run();
        runtime.cleanUp();
    }
    catch (const std::exception& e)
    {
        std::cerr << "FATAL: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
