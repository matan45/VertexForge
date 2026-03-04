#include "../handlers/RuntimeHandler.hpp"
#include "print/Log.hpp"
#include <iostream>
#include <filesystem>

int main(int argc, char* argv[])
{
    // Disable info/warning logs for shipped games
    util::loggingEnabled = false;

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
        std::cerr << "Runtime error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
