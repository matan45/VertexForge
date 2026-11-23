#include <doctest.h>

#include "launch/GameStateLocation.hpp"
#include "launch/Utf8Path.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

// ============================================================
// VK-1726: where a shipped game keeps its config, saves, logs and crash reports. A game installed under
// Program Files cannot write into its own folder, so by default it keeps them in
// %LOCALAPPDATA%\<Publisher>\<Game>; a portable export ("stateLocation": "portable") keeps them beside its
// .vfproj, as before. These cases pin the pure resolver; the paths in them are placeholders that are only
// compared, never touched (a placeholder drive may not be ready, VK-1732). Disk cases use the temp folder.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    // "משחק" (game), as UTF-16 and as UTF-8.
    const std::wstring kHebrewWide = L"\u05DE\u05E9\u05D7\u05E7";
    const std::string kHebrewUtf8 = "\xD7\x9E\xD7\xA9\xD7\x97\xD7\xA7";

    const fs::path kLocalAppData = fs::path(L"C:\\Users\\me\\AppData\\Local");
    const fs::path kGameFolder = fs::path(L"C:\\Games\\Space Hopper");
    const fs::path kProjectFile = kGameFolder / L"Space Hopper.vfproj";
    const fs::path kWorkingDir = fs::path(L"C:\\Work");

    std::string projectText(const std::string& members)
    {
        return "{\n  \"projectName\": \"Space Hopper\",\n  \"version\": \"1.0.0\"" + members + "\n}\n";
    }

    launch::GameStateInputs inputsWith(std::optional<std::string> text)
    {
        launch::GameStateInputs inputs;
        inputs.projectFile = kProjectFile;
        inputs.projectText = std::move(text);
        inputs.localAppDataDir = kLocalAppData;
        inputs.workingDir = kWorkingDir;
        return inputs;
    }

    void checkPortableAt(const launch::GameStateLocation& location, const fs::path& root)
    {
        CHECK_FALSE(location.isUser());
        CHECK(location.mode == launch::GameStateMode::Portable);
        CHECK(location.stateRoot == root);
        CHECK(location.logsDir == root / L"logs");
        CHECK(location.crashesDir == root / L"crashes");
    }

    void checkUserAt(const launch::GameStateLocation& location, const fs::path& root)
    {
        CHECK(location.isUser());
        CHECK(location.stateRoot == root);
        CHECK(location.logsDir == root / L"logs");
        CHECK(location.crashesDir == root / L"crashes");
    }

    bool anyProblemContains(const launch::GameStateLocation& location, const std::string& text)
    {
        for (const std::string& problem : location.problems)
        {
            if (problem.find(text) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    struct TempFolder
    {
        fs::path path;

        explicit TempFolder(const std::wstring& name)
        {
            path = fs::temp_directory_path() / (L"VertexForge_GameState_" + name + L"_" + kHebrewWide);
            std::error_code ec;
            fs::remove_all(path, ec);
            fs::create_directories(path, ec);
        }

        ~TempFolder()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    };

    void writeFile(const fs::path& file, const std::string& text)
    {
        std::ofstream out(file, std::ios::binary);
        out << text;
    }
}

TEST_SUITE("GameStateLocation")
{
    TEST_CASE("a shipped game keeps its state in LocalAppData under its publisher")
    {
        const auto location = launch::resolveGameStateLocation(
            inputsWith(projectText(",\n  \"publisher\": \"Acme Games\",\n  \"stateLocation\": \"user\"")));
        checkUserAt(location, kLocalAppData / L"Acme Games" / L"Space Hopper");
        CHECK(location.problems.empty());
    }

    TEST_CASE("without a publisher the game's folder sits right under LocalAppData")
    {
        const auto location = launch::resolveGameStateLocation(inputsWith(projectText(",\n  \"stateLocation\": \"user\"")));
        checkUserAt(location, kLocalAppData / L"Space Hopper");
        CHECK(location.problems.empty());

        const auto blank = launch::resolveGameStateLocation(inputsWith(projectText(",\n  \"publisher\": \"   \"")));
        checkUserAt(blank, kLocalAppData / L"Space Hopper");
        CHECK(blank.problems.empty());
    }

    TEST_CASE("a .vfproj without stateLocation counts as per-user, silently")
    {
        // Runtime.exe started on an editor project: the exporter always writes the member.
        const auto location = launch::resolveGameStateLocation(inputsWith(projectText("")));
        checkUserAt(location, kLocalAppData / L"Space Hopper");
        CHECK(location.problems.empty());
    }

    TEST_CASE("a portable game keeps its state beside its .vfproj")
    {
        const auto location = launch::resolveGameStateLocation(
            inputsWith(projectText(",\n  \"publisher\": \"Acme\",\n  \"stateLocation\": \"portable\"")));
        checkPortableAt(location, kGameFolder);
        CHECK(location.problems.empty());
    }

    TEST_CASE("an unknown or malformed stateLocation falls back to the per-user folder, with a problem")
    {
        const auto unknown = launch::resolveGameStateLocation(inputsWith(projectText(",\n  \"stateLocation\": \"Portable\"")));
        checkUserAt(unknown, kLocalAppData / L"Space Hopper");
        REQUIRE(unknown.problems.size() == 1);
        CHECK(unknown.problems[0].find("unknown \"stateLocation\" value \"Portable\"") != std::string::npos);
        CHECK(unknown.problems[0].find(launch::pathToUtf8(kProjectFile)) == 0);

        const auto notString = launch::resolveGameStateLocation(inputsWith(projectText(",\n  \"stateLocation\": true")));
        checkUserAt(notString, kLocalAppData / L"Space Hopper");
        REQUIRE(notString.problems.size() == 1);
        CHECK(notString.problems[0].find("\"stateLocation\" that is not a string") != std::string::npos);
    }

    TEST_CASE("a publisher that is not a string is ignored, with a problem")
    {
        const auto location = launch::resolveGameStateLocation(inputsWith(projectText(",\n  \"publisher\": 7")));
        checkUserAt(location, kLocalAppData / L"Space Hopper");
        REQUIRE(location.problems.size() == 1);
        CHECK(location.problems[0].find("\"publisher\" that is not a string") != std::string::npos);
    }

    TEST_CASE("a .vfproj that cannot be used keeps the state in the game's own folder")
    {
        const auto unreadable = launch::resolveGameStateLocation(inputsWith(std::nullopt));
        checkPortableAt(unreadable, kGameFolder);
        CHECK(anyProblemContains(unreadable, "could not be read"));

        const auto notJson = launch::resolveGameStateLocation(inputsWith(std::string("{ not json")));
        checkPortableAt(notJson, kGameFolder);
        CHECK(anyProblemContains(notJson, "is not a JSON object"));

        const auto array = launch::resolveGameStateLocation(inputsWith(std::string("[1, 2]")));
        checkPortableAt(array, kGameFolder);
        CHECK(anyProblemContains(array, "is not a JSON object"));

        const auto noName = launch::resolveGameStateLocation(inputsWith(std::string("{\"version\": \"1.0.0\"}")));
        checkPortableAt(noName, kGameFolder);
        CHECK(anyProblemContains(noName, "has no \"projectName\""));

        const auto emptyName = launch::resolveGameStateLocation(inputsWith(std::string("{\"projectName\": \"\"}")));
        checkPortableAt(emptyName, kGameFolder);
        CHECK(anyProblemContains(emptyName, "has no \"projectName\""));
    }

    TEST_CASE("without a project the game keeps its state in the working directory")
    {
        launch::GameStateInputs inputs = inputsWith(std::nullopt);
        inputs.projectFile.clear();
        const auto location = launch::resolveGameStateLocation(inputs);
        checkPortableAt(location, kWorkingDir);
        CHECK(anyProblemContains(location, "no .vfproj was given"));
    }

    TEST_CASE("without LocalAppData a per-user game keeps its state in its own folder")
    {
        launch::GameStateInputs inputs = inputsWith(projectText(",\n  \"stateLocation\": \"user\""));
        inputs.localAppDataDir.clear();
        const auto location = launch::resolveGameStateLocation(inputs);
        checkPortableAt(location, kGameFolder);
        CHECK(anyProblemContains(location, "LocalAppData"));
    }

    TEST_CASE("a UTF-8 BOM and a Hebrew name are read")
    {
        const std::string text = "\xEF\xBB\xBF{\"projectName\": \"" + kHebrewUtf8 + "\", \"publisher\": \"" + kHebrewUtf8 +
                                 "\", \"stateLocation\": \"user\"}";
        const launch::GameIdentityRead read = launch::readGameIdentity(text);
        REQUIRE(read.identity.has_value());
        CHECK(read.identity->projectName == kHebrewUtf8);
        CHECK(read.identity->publisher == kHebrewUtf8);
        REQUIRE(read.identity->stateLocation.has_value());
        CHECK(*read.identity->stateLocation == "user");
        CHECK(read.problems.empty());

        const auto location = launch::resolveGameStateLocation(inputsWith(text));
        checkUserAt(location, kLocalAppData / kHebrewWide / kHebrewWide);
    }

    TEST_CASE("readGameIdentity reports absent members as absent")
    {
        const launch::GameIdentityRead read = launch::readGameIdentity("{\"projectName\": \"Game\"}");
        REQUIRE(read.identity.has_value());
        CHECK(read.identity->publisher.empty());
        CHECK_FALSE(read.identity->stateLocation.has_value());
        CHECK(read.problems.empty());
    }

    TEST_CASE("the folder names are made safe for Windows")
    {
        const auto illegal = launch::gameStateFolderName("Space: Hopper?", "Acme/Games");
        CHECK(illegal.relative == fs::path(L"Acme_Games") / L"Space_ Hopper_");
        CHECK(illegal.problems.empty());

        const auto reserved = launch::gameStateFolderName("CON", "nul");
        CHECK(reserved.relative == fs::path(L"nul_") / L"CON_");

        const auto trimmed = launch::gameStateFolderName("  Game.  ", " .Acme. ");
        CHECK(trimmed.relative == fs::path(L"Acme") / L"Game");

        const auto nothingLeft = launch::gameStateFolderName("...", "...");
        CHECK(nothingLeft.relative == fs::path(L"Game"));
        CHECK(nothingLeft.problems.size() == 2);
    }

    TEST_CASE("long names are cut to maxGameFolderNameUnits on a code point boundary")
    {
        const std::string longName(100, 'g');
        const auto location = launch::gameStateFolderName(longName, std::string(100, 'p'));
        CHECK(location.relative == fs::path(std::wstring(launch::maxGameFolderNameUnits, L'p')) /
                                       std::wstring(launch::maxGameFolderNameUnits, L'g'));

        // An emoji (a surrogate pair) at the cut point is left out whole.
        const std::string emoji = "\xF0\x9F\x8E\xAE";
        const auto cut = launch::gameStateFolderName(std::string(launch::maxGameFolderNameUnits - 1, 'g') + emoji, "");
        CHECK(cut.relative == fs::path(std::wstring(launch::maxGameFolderNameUnits - 1, L'g')));

        // With both names at the limit the crash folder fits util::setCrashDirectory's 220 units for a LocalAppData
        // path of 82 units.
        const fs::path localAppData = fs::path(L"C:\\") / std::wstring(82 - 3, L'a');
        REQUIRE(localAppData.native().size() == 82);
        launch::GameStateInputs inputs = inputsWith("{\"projectName\": \"" + longName + "\", \"publisher\": \"" +
                                                    std::string(100, 'p') + "\"}");
        inputs.localAppDataDir = localAppData;
        const auto longest = launch::resolveGameStateLocation(inputs);
        CHECK(longest.crashesDir.native().size() == 220);
    }

    TEST_CASE("findGameProjectFile returns the first .vfproj spelled as folder / name")
    {
        TempFolder folder(L"Find");
        CHECK(launch::findGameProjectFile(folder.path).empty());

        writeFile(folder.path / L"readme.txt", "x");
        CHECK(launch::findGameProjectFile(folder.path).empty());

        writeFile(folder.path / (kHebrewWide + L".vfproj"), "{}");
        CHECK(launch::findGameProjectFile(folder.path) == folder.path / (kHebrewWide + L".vfproj"));

        CHECK(launch::findGameProjectFile(folder.path / L"missing").empty());
    }

    TEST_CASE("createGameStateFolders creates the per-user folders and nothing for a portable game")
    {
        TempFolder folder(L"Create");
        launch::GameStateInputs inputs = inputsWith(projectText(",\n  \"publisher\": \"Acme\""));
        inputs.localAppDataDir = folder.path;
        const auto user = launch::resolveGameStateLocation(inputs);
        REQUIRE(user.isUser());
        CHECK(launch::createGameStateFolders(user).empty());
        std::error_code ec;
        CHECK(fs::is_directory(folder.path / L"Acme" / L"Space Hopper" / L"logs", ec));
        CHECK(fs::is_directory(folder.path / L"Acme" / L"Space Hopper" / L"crashes", ec));

        launch::GameStateLocation portable;
        portable.stateRoot = folder.path / L"portable";
        portable.logsDir = portable.stateRoot / L"logs";
        portable.crashesDir = portable.stateRoot / L"crashes";
        CHECK(launch::createGameStateFolders(portable).empty());
        CHECK_FALSE(fs::exists(portable.stateRoot, ec));
    }

    TEST_CASE("processGameState reads the .vfproj it is given")
    {
        TempFolder folder(L"Process");
        const fs::path project = folder.path / L"Game.vfproj";
        writeFile(project, "{\"projectName\": \"Game\", \"stateLocation\": \"portable\"}");

        const auto portable = launch::processGameState(launch::pathToUtf8(project));
        checkPortableAt(portable, folder.path);
        CHECK(portable.problems.empty());

        writeFile(project, "{\"projectName\": \"Game\", \"publisher\": \"Acme\", \"stateLocation\": \"user\"}");
        const auto user = launch::processGameState(launch::pathToUtf8(project));
        if (user.isUser())
        {
            CHECK(user.stateRoot.is_absolute());
            CHECK(user.stateRoot.filename() == L"Game");
            CHECK(user.stateRoot.parent_path().filename() == L"Acme");
        }
        else
        {
            // No LocalAppData known folder on this machine: the game's own folder, with the reason.
            checkPortableAt(user, folder.path);
            CHECK(anyProblemContains(user, "LocalAppData"));
        }

        const auto missing = launch::processGameState(launch::pathToUtf8(folder.path / L"Missing.vfproj"));
        checkPortableAt(missing, folder.path);
        CHECK(anyProblemContains(missing, "could not be read"));
    }
}
