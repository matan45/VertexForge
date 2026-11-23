#include "../handlers/RuntimeHandler.hpp"
#include "crash/CrashHandler.hpp"
#include "launch/GameStateLocation.hpp"
#include "launch/ProcessCodePage.hpp"
#include "launch/Utf8Path.hpp"
#include "print/Log.hpp"
#include <iostream>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>


// Runtime.exe [project.vfproj]
//
// Without an argument the game opens the first .vfproj in the working directory.
//
// Runtime.exe embeds the UTF-8 code page manifest (VK-1684, launch/Utf8CodePage.manifest): the project
// path, like every narrow string in the process, is UTF-8 and may hold any character (Windows 10
// version 1903 or later).
//
// State (VK-1726, launch/GameStateLocation.hpp): config.json, the save slots, logs/ and crashes/ go in
// %LOCALAPPDATA%\<Publisher>\<Game> (a game installed under Program Files cannot write into its own
// folder), or beside the .vfproj for a portable build ("stateLocation": "portable").
int main(int argc, char* argv[])
{
    // A console shows the UTF-8 output as UTF-8 (VK-1684). Declared first, so destroyed last.
    launch::Utf8ConsoleOutput utf8Console;

    // Install the crash handler first (so even a fault while working out the folders is dumped).
    util::installCrashHandler();

    // The project as the game opens it: kept as spelled (".\Game.vfproj"), because the .vfpak's keys are
    // relative to the working directory.
    std::string projectPath;
    if (argc > 1)
    {
        projectPath = argv[1];
    }
    else
    {
        try
        {
            projectPath = launch::findGameProjectFile(std::filesystem::path(L".")).string();
        }
        catch (const std::exception&)
        {
            projectPath.clear();  // a name the process code page cannot spell: no project, as with none at all
        }
    }

    // VK-1726: where the game keeps its player's state, decided before the log file opens; what went wrong is
    // logged once the log exists.
    const launch::GameStateLocation gameState = launch::processGameState(projectPath);
    std::vector<std::string> stateProblems = gameState.problems;
    for (std::string& problem : launch::createGameStateFolders(gameState))
    {
        stateProblems.push_back(std::move(problem));
    }
    if (gameState.crashesDir.is_absolute() && !util::setCrashDirectory(gameState.crashesDir))
    {
        stateProblems.push_back("the crash folder " + launch::pathToUtf8(gameState.crashesDir) +
                                " is too long: crash reports go into crashes/ under the working directory");
    }

    util::initLogFile("Runtime", gameState.logsDir);
    for (const std::string& problem : stateProblems)
    {
        // An error, not a warning: Release builds keep only errors (util::engineLogsEnabled).
        vfLogError("{}", problem);
    }
    vfLogInfo("Game state: {} (settings, save slots, logs, crash reports)",
              launch::pathToUtf8(gameState.stateRoot.empty() ? std::filesystem::path(L".") : gameState.stateRoot));

    if (!launch::processUsesUtf8())
    {
        // An error, not a warning: Release builds keep only errors (util::engineLogsEnabled).
        vfLogError("Runtime.exe runs in code page {}, not UTF-8 (Windows 10 version 1903 or later honours its "
                   "UTF-8 manifest): paths with characters outside that code page cannot be opened",
                   launch::activeCodePage());
    }

    handlers::RuntimeHandler runtime(gameState.stateRoot);

    try
    {
        runtime.init();

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
