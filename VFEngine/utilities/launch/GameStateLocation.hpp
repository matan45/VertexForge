#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// VK-1726: where a shipped game (Runtime.exe) keeps the state its player makes: config.json (ConfigService), the
// save slots (SaveService's saves/), and its logs/ and crashes/.
//
// A game installed under Program Files cannot write into its own folder, so a shipped game keeps that state in
//
//   %LOCALAPPDATA%\<Publisher>\<Game>        (%LOCALAPPDATA%\<Game> when the project names no publisher)
//
// <Game> is the shipped .vfproj's "projectName" (the exporter writes the game's name there) and <Publisher> its
// "publisher" (Project Settings). A portable export (the Export window's "Portable build") writes
//
//   "stateLocation": "portable"
//
// into the shipped .vfproj, and the game then keeps all of it in the .vfproj's own folder, as before VK-1726. The
// exporter always writes the member ("user" or "portable"); a .vfproj without it (Runtime.exe started on an editor
// project) counts as "user".
//
// Runtime.exe works this out in main(), before its log file opens. The editor never uses it: its Play mode keeps
// saves and config in <project>/.vertexforge (VK-1684), and its own state goes where launch::processUserState()
// says (VK-1732).
//
// This header carries no Win32 types: it is exported into the plugin SDK.
namespace launch
{
    // The .vfproj members (ProjectSerialization reads and writes them under these names).
    inline constexpr std::string_view projectPublisherKey = "publisher";
    inline constexpr std::string_view projectStateLocationKey = "stateLocation";
    inline constexpr std::string_view gameStateLocationUser = "user";
    inline constexpr std::string_view gameStateLocationPortable = "portable";

    // Each folder name (the publisher's, the game's) is cut to this many UTF-16 units. With both at the limit the
    // crash folder stays within util::setCrashDirectory's 220 units for any LocalAppData path up to 82 units.
    inline constexpr std::size_t maxGameFolderNameUnits = 64;
    // The folder of a game whose name leaves nothing usable.
    inline constexpr std::string_view fallbackGameFolderName = "Game";

    enum class GameStateMode
    {
        User,     // <LocalAppData>/<Publisher>/<Game>
        Portable  // the .vfproj's folder (the working directory when there is no project)
    };

    // The .vfproj members the state folder depends on.
    struct GameIdentity
    {
        std::string projectName;
        std::string publisher;                     // empty: none
        std::optional<std::string> stateLocation;  // nullopt: absent, or not a string (a problem)
    };

    struct GameIdentityRead
    {
        std::optional<GameIdentity> identity;  // nullopt: not a JSON object, or no usable projectName
        std::vector<std::string> problems;     // without the file's name (the caller adds it)
    };

    // Pure: the members from a .vfproj's text (a UTF-8 BOM is skipped). Never throws.
    GameIdentityRead readGameIdentity(std::string_view projectText);

    struct GameFolderName
    {
        std::filesystem::path relative;     // "<Publisher>/<Game>", or "<Game>"
        std::vector<std::string> problems;  // a name that left nothing usable
    };

    // Pure: the game's folder under LocalAppData. Each name goes through strutil::toSafeFolderName (illegal
    // characters become '_', reserved device names are escaped, at most maxGameFolderNameUnits); a game name that
    // leaves nothing becomes fallbackGameFolderName and a publisher that leaves nothing is dropped, each with a
    // problem. Never throws.
    GameFolderName gameStateFolderName(std::string_view projectName, std::string_view publisher);

    struct GameStateInputs
    {
        std::filesystem::path projectFile;       // the .vfproj, absolute; empty: no project
        std::optional<std::string> projectText;  // its text; nullopt: it could not be read
        std::filesystem::path localAppDataDir;   // FOLDERID_LocalAppData; empty: unknown
        std::filesystem::path workingDir;        // the current directory, absolute
    };

    struct GameStateLocation
    {
        GameStateMode mode = GameStateMode::Portable;
        // User: <LocalAppData>/<folder name>. Portable: the .vfproj's folder, or the working directory when there is
        // no project. Empty only when processGameState failed outright: the services then keep their files beside
        // the .vfproj and the logs and crash reports go to logs/ and crashes/ under the working directory, as before.
        std::filesystem::path stateRoot;
        std::filesystem::path logsDir;      // <root>/logs     ("logs" when the root is empty)
        std::filesystem::path crashesDir;   // <root>/crashes  ("crashes" when the root is empty)
        // What went wrong while deciding, for the log: this runs before the log file exists.
        std::vector<std::string> problems;

        bool isUser() const noexcept { return mode == GameStateMode::User; }
    };

    // Pure: decides from the inputs alone, with lexical path operations only (it never touches the disk). Never
    // throws for bad project text.
    GameStateLocation resolveGameStateLocation(const GameStateInputs& inputs);

    // The first *.vfproj directly in `folder` (spelled `folder` / its name, so "." gives ".\Game.vfproj"); empty
    // when there is none or the folder cannot be listed. Never throws.
    std::filesystem::path findGameProjectFile(const std::filesystem::path& folder);

    // The answer for this process: reads `projectArgument` (the .vfproj as Runtime.exe opens it, UTF-8, relative to
    // the current directory; empty: no project), asks Windows for LocalAppData, then resolves. Never throws: anything
    // unexpected gives Portable with an empty root and a problem.
    GameStateLocation processGameState(std::string_view projectArgument) noexcept;

    // User mode: creates the state root, logs/ and crashes/, and returns a problem per folder it could not create.
    // Portable: does nothing (the log file and the crash handler create their folders as before).
    std::vector<std::string> createGameStateFolders(const GameStateLocation& location);
}
