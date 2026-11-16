#include "../handlers/RuntimeHandler.hpp"
#include "crash/CrashHandler.hpp"
#include "launch/ProcessCodePage.hpp"
#include "print/Log.hpp"
#include <iostream>
#include <filesystem>


// Runtime.exe [project.vfproj]
//
// Runtime.exe embeds the UTF-8 code page manifest (VK-1684, launch/Utf8CodePage.manifest): the project
// path, like every narrow string in the process, is UTF-8 and may hold any character (Windows 10
// version 1903 or later).
int main(int argc, char* argv[])
{
    // A console shows the UTF-8 output as UTF-8 (VK-1684). Declared first, so destroyed last.
    launch::Utf8ConsoleOutput utf8Console;

    // Install the crash handler first (so even a fault during log init is
    // dumped), then mirror the log to disk.
    util::installCrashHandler();
    util::initLogFile("Runtime");

    if (!launch::processUsesUtf8())
    {
        // An error, not a warning: Release builds keep only errors (util::engineLogsEnabled).
        vfLogError("Runtime.exe runs in code page {}, not UTF-8 (Windows 10 version 1903 or later honours its "
                   "UTF-8 manifest): paths with characters outside that code page cannot be opened",
                   launch::activeCodePage());
    }

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
