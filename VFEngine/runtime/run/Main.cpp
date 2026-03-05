#include "../handlers/RuntimeHandler.hpp"
#include "print/Log.hpp"
#include <filesystem>



int main(int argc, char* argv[])
{
    handlers::RuntimeHandler runtime;
    util::loggingEnabled = false;
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
                vfLogError("Failed to load project: {}", projectPath);
            }
        }

        runtime.run();
        runtime.cleanUp();
    }
    catch (const std::exception& e)
    {
        vfLogError("FATAL: {}", e.what());
        return 1;
    }

    return 0;
}
