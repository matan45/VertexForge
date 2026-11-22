#include <doctest.h>

#include "launch/UserStateLocation.hpp"
#include "launch/Utf8Path.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

// ============================================================
// VK-1732: where the editor keeps the state its user makes. The release package writes
// "stateLocation": "user" into the engine.json beside Editor.exe, and the editor then keeps logs,
// crash reports, imgui.ini and plugin data in %USERPROFILE%\.vertexforge\editor (the MSI deletes the
// install folder on every upgrade). Without the member, or with "engine", everything stays where it
// was before VK-1732: the dev tree, the Runtime and Tests.exe.
// ============================================================

namespace
{
    namespace fs = std::filesystem;
    using launch::StateLocation;
    using launch::UserStateInputs;
    using launch::UserStateLocation;

    // "שלום" (shalom), so the per-user folders are checked with a name outside any ANSI code page.
    const std::wstring kHebrewWide = L"\u05E9\u05DC\u05D5\u05DD";

    UserStateInputs inputsWith(std::optional<std::string> manifest)
    {
        UserStateInputs inputs;
        inputs.engineManifestText = std::move(manifest);
        inputs.profileDir = fs::path(L"C:\\Users\\me");
        inputs.documentsDir = fs::path(L"D:\\Docs");
        inputs.exeDir = fs::path(L"C:\\Engine\\bin\\Editor\\Release\\x64");
        inputs.workingDir = fs::path(L"C:\\Work");
        return inputs;
    }

    void checkEngineLayout(const UserStateLocation& location, const UserStateInputs& inputs)
    {
        CHECK(location.mode == StateLocation::Engine);
        CHECK_FALSE(location.isUser());
        CHECK(location.logsDir == inputs.workingDir / L"logs");
        CHECK(location.crashesDir == inputs.workingDir / L"crashes");
        CHECK(location.imguiIniFile == inputs.exeDir / L"imgui.ini");
        CHECK(location.stateRoot.empty());
        CHECK(location.pluginDataDir.empty());
        CHECK(location.pluginOverridesFile.empty());
        CHECK(location.savesFallbackDir.empty());
        CHECK(location.diagnosticsDir.empty());
        CHECK(location.userPluginsDir.empty());
    }

    struct TempFolder
    {
        fs::path path;

        explicit TempFolder(const std::wstring& name)
        {
            path = fs::temp_directory_path() / (name + L"_" + kHebrewWide);
            std::error_code ec;
            fs::remove_all(path, ec);
            fs::create_directories(path);
        }

        ~TempFolder()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    };
}

TEST_SUITE("UserStateLocation")
{
    TEST_CASE("no engine.json keeps the state beside the engine, silently")
    {
        const UserStateInputs inputs = inputsWith(std::nullopt);
        const UserStateLocation location = launch::resolveUserStateLocation(inputs);
        checkEngineLayout(location, inputs);
        CHECK(location.problems.empty());
    }

    TEST_CASE("\"engine\" and a manifest without the member keep the state beside the engine")
    {
        for (const char* manifest : {R"({"manifestVersion":1,"stateLocation":"engine"})",
                                     R"({"manifestVersion":1,"workingDirectory":"."})"})
        {
            CAPTURE(manifest);
            const UserStateInputs inputs = inputsWith(std::string(manifest));
            const UserStateLocation location = launch::resolveUserStateLocation(inputs);
            checkEngineLayout(location, inputs);
            CHECK(location.problems.empty());
        }
    }

    TEST_CASE("\"user\" puts every folder under the profile and the plugins in Documents")
    {
        const UserStateInputs inputs = inputsWith(std::string(R"({"manifestVersion":1,"stateLocation":"user"})"));
        const UserStateLocation location = launch::resolveUserStateLocation(inputs);

        CHECK(location.mode == StateLocation::User);
        CHECK(location.isUser());
        CHECK(location.problems.empty());
        const fs::path root = fs::path(L"C:\\Users\\me\\.vertexforge\\editor");
        CHECK(location.stateRoot == root);
        CHECK(location.logsDir == root / L"logs");
        CHECK(location.crashesDir == root / L"crashes");
        CHECK(location.imguiIniFile == root / L"imgui.ini");
        CHECK(location.pluginDataDir == root / L"plugin-data");
        CHECK(location.pluginOverridesFile == root / L"plugin-overrides.json");
        CHECK(location.savesFallbackDir == root / L"saves");
        CHECK(location.diagnosticsDir == root / L"diagnostics");
        CHECK(location.userPluginsDir == fs::path(L"D:\\Docs\\VertexForge\\Plugins"));
    }

    TEST_CASE("the engine root is four levels above the exe folder")
    {
        // bin/Editor/<Config>/x64 in the dev tree and in the release bundle alike; it holds sdk/ and plugins/.
        for (const char* manifest : {R"({"stateLocation":"engine"})", R"({"stateLocation":"user"})"})
        {
            CAPTURE(manifest);
            const UserStateLocation location = launch::resolveUserStateLocation(inputsWith(std::string(manifest)));
            CHECK(location.engineRoot == fs::path(L"C:\\Engine"));
        }
    }

    TEST_CASE("a UTF-8 BOM before the manifest is accepted")
    {
        const UserStateLocation location =
            launch::resolveUserStateLocation(inputsWith(std::string("\xEF\xBB\xBF{\"stateLocation\":\"user\"}")));
        CHECK(location.isUser());
        CHECK(location.problems.empty());
    }

    TEST_CASE("a manifest the editor cannot use keeps the state beside the engine, with a problem")
    {
        for (const char* manifest : {"{not json", "[\"stateLocation\",\"user\"]", R"({"stateLocation":1})",
                                     R"({"stateLocation":null})", R"({"stateLocation":"User"})",
                                     R"({"stateLocation":"portable"})"})
        {
            CAPTURE(manifest);
            const UserStateInputs inputs = inputsWith(std::string(manifest));
            const UserStateLocation location = launch::resolveUserStateLocation(inputs);
            checkEngineLayout(location, inputs);
            REQUIRE(location.problems.size() == 1);
            CHECK(location.problems[0].find("engine.json") != std::string::npos);
        }

        const UserStateLocation unknown =
            launch::resolveUserStateLocation(inputsWith(std::string(R"({"stateLocation":"portable"})")));
        REQUIRE(unknown.problems.size() == 1);
        CHECK(unknown.problems[0].find("\"portable\"") != std::string::npos);
    }

    TEST_CASE("an engine.json that cannot be read keeps the state beside the engine, with a problem")
    {
        UserStateInputs inputs = inputsWith(std::nullopt);
        inputs.engineManifestUnreadable = true;
        const UserStateLocation location = launch::resolveUserStateLocation(inputs);
        checkEngineLayout(location, inputs);
        REQUIRE(location.problems.size() == 1);
        CHECK(location.problems[0].find("could not be read") != std::string::npos);
    }

    TEST_CASE("\"user\" without a profile folder keeps the state beside the engine")
    {
        UserStateInputs inputs = inputsWith(std::string(R"({"stateLocation":"user"})"));
        inputs.profileDir.clear();
        const UserStateLocation location = launch::resolveUserStateLocation(inputs);
        checkEngineLayout(location, inputs);
        REQUIRE(location.problems.size() == 1);
        CHECK(location.problems[0].find("USERPROFILE") != std::string::npos);
    }

    TEST_CASE("\"user\" without a Documents folder puts the plugins under the profile")
    {
        UserStateInputs inputs = inputsWith(std::string(R"({"stateLocation":"user"})"));
        inputs.documentsDir.clear();
        const UserStateLocation location = launch::resolveUserStateLocation(inputs);
        CHECK(location.isUser());
        CHECK(location.userPluginsDir == fs::path(L"C:\\Users\\me\\Documents\\VertexForge\\Plugins"));
        REQUIRE(location.problems.size() == 1);
        CHECK(location.problems[0].find("Documents") != std::string::npos);
    }

    TEST_CASE("a profile folder outside any ANSI code page is kept as it is")
    {
        UserStateInputs inputs = inputsWith(std::string(R"({"stateLocation":"user"})"));
        inputs.profileDir = fs::path(L"C:\\Users\\" + kHebrewWide);
        const UserStateLocation location = launch::resolveUserStateLocation(inputs);
        CHECK(location.crashesDir == fs::path(L"C:\\Users\\" + kHebrewWide + L"\\.vertexforge\\editor\\crashes"));
        CHECK(launch::pathToUtf8(location.stateRoot) ==
              "C:\\Users\\\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D\\.vertexforge\\editor");
    }

    TEST_CASE("Tests.exe, like the Runtime, has no engine.json and keeps the state beside the engine")
    {
        // The accessor reads the engine.json beside the PROCESS executable: Tests.exe has none (only the
        // Editor's postbuild writes one), so this pins the default every non-editor binary gets.
        const UserStateLocation& location = launch::processUserState();
        CHECK(&location == &launch::processUserState());
        CHECK_FALSE(location.isUser());
        CHECK(location.problems.empty());

        std::error_code ec;
        const fs::path cwd = fs::current_path(ec);
        REQUIRE_FALSE(ec);
        CHECK(location.logsDir == cwd / L"logs");
        CHECK(location.crashesDir == cwd / L"crashes");
        CHECK(location.imguiIniFile.filename() == fs::path(L"imgui.ini"));
        CHECK(location.userPluginsDir.empty());
    }

    TEST_CASE("createUserStateFolders makes the per-user folders, and nothing in Engine mode")
    {
        TempFolder temp(L"VertexForge_UserState");

        UserStateInputs engineInputs = inputsWith(std::nullopt);
        engineInputs.workingDir = temp.path / L"work";
        CHECK(launch::createUserStateFolders(launch::resolveUserStateLocation(engineInputs)).empty());
        CHECK_FALSE(fs::exists(engineInputs.workingDir));

        UserStateInputs userInputs = inputsWith(std::string(R"({"stateLocation":"user"})"));
        userInputs.profileDir = temp.path / L"profile";
        const UserStateLocation location = launch::resolveUserStateLocation(userInputs);
        CHECK(launch::createUserStateFolders(location).empty());
        CHECK(fs::is_directory(location.stateRoot));
        CHECK(fs::is_directory(location.logsDir));
        CHECK(fs::is_directory(location.crashesDir));
        // The rest is made by whoever writes there first.
        CHECK_FALSE(fs::exists(location.pluginDataDir));
        CHECK_FALSE(fs::exists(location.userPluginsDir));
    }
}
