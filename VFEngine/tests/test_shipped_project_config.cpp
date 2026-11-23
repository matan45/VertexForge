#include <doctest.h>

#include "config/EditorPreferencesSerializer.hpp"
#include "config/ProjectConfig.hpp"
#include "export/ExportConfig.hpp"
#include "export/ShippedProjectConfig.hpp"
#include "launch/GameStateLocation.hpp"
#include "serialization/ProjectSerialization.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>

// ============================================================
// VK-1726: the shipped .vfproj tells Runtime.exe where the game keeps its config, saves, logs and crash
// reports. The exporter writes "publisher" (Project Settings) and always "stateLocation" ("user" or
// "portable", the Export window's "Portable build"); Runtime.exe reads them again before its log opens with
// launch::readGameIdentity, a separate reader from ProjectSerialization's. These cases pin that the editor's
// writer, the editor's reader and the Runtime's reader agree.
// ============================================================

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    // "שמירה" (saving), as UTF-8.
    const std::string kHebrewUtf8 = "\xD7\xA9\xD7\x9E\xD7\x99\xD7\xA8\xD7\x94";

    struct TempFolder
    {
        fs::path folder;

        explicit TempFolder(const char* name)
        {
            folder = fs::temp_directory_path() / name;
            std::error_code ec;
            fs::remove_all(folder, ec);
            fs::create_directories(folder, ec);
        }

        ~TempFolder()
        {
            std::error_code ec;
            fs::remove_all(folder, ec);
        }
    };

    config::ProjectConfig makeProject()
    {
        config::ProjectConfig project;
        project.projectName = "Shipped Game";
        project.version = "1.2.3";
        project.workingDirectory = "Assets";
        project.startupScene = "scenes/Main.vfScene";
        return project;
    }

    std::string readText(const fs::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        REQUIRE(in.is_open());
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    void writeText(const fs::path& file, const std::string& text)
    {
        std::ofstream out(file, std::ios::binary);
        REQUIRE(out.is_open());
        out << text;
    }

    // Saves `project`, then reads the file back with both readers and checks they report what was saved.
    void checkWritersAndReadersAgree(const config::ProjectConfig& project, const fs::path& file)
    {
        REQUIRE(serialization::ProjectSerialization::saveProject(project, file.string()));
        const std::string text = readText(file);

        const json written = json::parse(text);
        CHECK(written.contains(std::string(launch::projectPublisherKey)) == !project.publisher.empty());
        CHECK(written.contains(std::string(launch::projectStateLocationKey)) == project.stateLocation.has_value());

        const launch::GameIdentityRead runtimeRead = launch::readGameIdentity(text);
        REQUIRE(runtimeRead.identity.has_value());
        CHECK(runtimeRead.problems.empty());
        CHECK(runtimeRead.identity->projectName == project.projectName);
        CHECK(runtimeRead.identity->publisher == project.publisher);
        CHECK(runtimeRead.identity->stateLocation == project.stateLocation);

        const serialization::ProjectInspection editorRead =
            serialization::ProjectSerialization::inspectProject(file.string());
        REQUIRE(editorRead.ok());
        CHECK(editorRead.warnings.empty());
        CHECK(editorRead.config->projectName == project.projectName);
        CHECK(editorRead.config->publisher == project.publisher);
        CHECK(editorRead.config->stateLocation == project.stateLocation);
    }
}

TEST_SUITE("ShippedProjectConfig")
{
    TEST_CASE("the shipped .vfproj names the publisher and always says where the game keeps its state")
    {
        gameExport::ExportConfig config;
        config.gameName = "Shipped Game";
        config.gameVersion = "1.2.3";
        config.startupScene = "scenes/Main.vfScene";
        config.publisher = "Acme Games";
        config.expectedPluginApiVersion = 26;

        SUBCASE("a per-user build")
        {
            const config::ProjectConfig project = gameExport::shippedProjectConfig(config);
            CHECK(project.projectName == "Shipped Game");
            CHECK(project.version == "1.2.3");
            CHECK(project.workingDirectory == "Assets");
            CHECK(project.startupScene == "scenes/Main.vfScene");
            CHECK(project.publisher == "Acme Games");
            REQUIRE(project.stateLocation.has_value());
            CHECK(*project.stateLocation == launch::gameStateLocationUser);
            REQUIRE(project.pluginApiVersion.has_value());
            CHECK(*project.pluginApiVersion == 26u);
        }
        SUBCASE("a portable build")
        {
            config.portableBuild = true;
            const config::ProjectConfig project = gameExport::shippedProjectConfig(config);
            REQUIRE(project.stateLocation.has_value());
            CHECK(*project.stateLocation == launch::gameStateLocationPortable);
            CHECK(project.publisher == "Acme Games");
        }
        SUBCASE("no publisher")
        {
            config.publisher.clear();
            const config::ProjectConfig project = gameExport::shippedProjectConfig(config);
            CHECK(project.publisher.empty());
            REQUIRE(project.stateLocation.has_value());
            CHECK(*project.stateLocation == launch::gameStateLocationUser);
        }
        SUBCASE("an icon outside the project ships as its file name only")
        {
            config.iconPath = (fs::temp_directory_path() / "Icons" / "game.ico").string();
            CHECK(gameExport::shippedProjectConfig(config).exeIconPath == "game.ico");
            config.iconPath = "icons/game.ico";
            CHECK(gameExport::shippedProjectConfig(config).exeIconPath == "icons/game.ico");
        }
    }

    TEST_CASE("saveProject, inspectProject and the Runtime's readGameIdentity agree on publisher and stateLocation")
    {
        TempFolder temp("VertexForge_ShippedProjectConfig_Twin");
        const fs::path file = temp.folder / "Game.vfproj";

        SUBCASE("both members set")
        {
            config::ProjectConfig project = makeProject();
            project.publisher = "Acme Games";
            project.stateLocation = std::string(launch::gameStateLocationPortable);
            checkWritersAndReadersAgree(project, file);
        }
        SUBCASE("both members absent")
        {
            checkWritersAndReadersAgree(makeProject(), file);
        }
        SUBCASE("a Hebrew publisher, per-user")
        {
            config::ProjectConfig project = makeProject();
            project.publisher = kHebrewUtf8;
            project.stateLocation = std::string(launch::gameStateLocationUser);
            checkWritersAndReadersAgree(project, file);
        }
        SUBCASE("what the exporter writes")
        {
            gameExport::ExportConfig config;
            config.gameName = "Shipped Game";
            config.gameVersion = "1.2.3";
            config.startupScene = "scenes/Main.vfScene";
            config.publisher = "Acme Games";
            checkWritersAndReadersAgree(gameExport::shippedProjectConfig(config), file);
            config.portableBuild = true;
            checkWritersAndReadersAgree(gameExport::shippedProjectConfig(config), file);
        }
    }

    TEST_CASE("a publisher or stateLocation that is not a string is ignored by both readers")
    {
        TempFolder temp("VertexForge_ShippedProjectConfig_Lenient");
        const fs::path file = temp.folder / "Game.vfproj";

        const json document = {
            {"schemaVersion", config::ProjectSchemaVersion::toString()},
            {"projectName", "Shipped Game"},
            {"version", "1.2.3"},
            {"workingDirectory", "Assets"},
            {"startupScene", "scenes/Main.vfScene"},
            {std::string(launch::projectPublisherKey), 42},
            {std::string(launch::projectStateLocationKey), true}
        };
        writeText(file, document.dump(2));

        const serialization::ProjectInspection editorRead =
            serialization::ProjectSerialization::inspectProject(file.string());
        REQUIRE(editorRead.ok());
        CHECK(editorRead.warnings.size() == 2);
        CHECK(editorRead.config->publisher.empty());
        CHECK_FALSE(editorRead.config->stateLocation.has_value());

        const launch::GameIdentityRead runtimeRead = launch::readGameIdentity(document.dump(2));
        REQUIRE(runtimeRead.identity.has_value());
        CHECK(runtimeRead.identity->publisher.empty());
        CHECK_FALSE(runtimeRead.identity->stateLocation.has_value());
    }

    TEST_CASE("the Export window's portable build choice survives the editor settings file")
    {
        CHECK_FALSE(config::ExportSettings{}.portableBuild);

        config::ExportSettings settings;
        settings.portableBuild = true;
        const json serialized = settings;
        CHECK(serialized["portableBuild"] == true);
        CHECK(serialized.get<config::ExportSettings>().portableBuild);

        const json legacy = {{"cleanBuild", true}};
        const config::ExportSettings restored = legacy.get<config::ExportSettings>();
        CHECK_FALSE(restored.portableBuild);
        CHECK(restored.cleanBuild);
    }
}
