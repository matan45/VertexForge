#include <doctest.h>

#include "events/EventDispatcher.hpp"
#include "events/project/ProjectEvents.hpp"
#include "impl/project/ProjectServiceImpl.hpp"
#include "serialization/ProjectSerialization.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    config::ProjectConfig makeValidProjectConfig(const std::filesystem::path& workingDirectory)
    {
        config::ProjectConfig config;
        config.projectName = "ProjectServiceTest";
        config.version = "1.0";
        config.workingDirectory = workingDirectory.string();
        config.startupScene = "Startup.vfScene";
        return config;
    }

    std::filesystem::path makeTestDirectory(const char* name)
    {
        auto directory = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        return directory;
    }

    struct DispatcherScope
    {
        DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
        }

        ~DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
        }
    };

    // VK-1687 helpers.
    std::string readText(const std::filesystem::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    // The "workingDirectory" string exactly as the .vfproj on disk holds it.
    std::string savedWorkingDirectory(const std::filesystem::path& projectFile)
    {
        return nlohmann::json::parse(readText(projectFile)).at("workingDirectory").get<std::string>();
    }

    // Writes a .vfproj whose workingDirectory is exactly `workingDirectory`.
    void writeProject(const std::filesystem::path& projectFile, const std::string& workingDirectory)
    {
        config::ProjectConfig config = makeValidProjectConfig({});
        config.workingDirectory = workingDirectory;
        REQUIRE(serialization::ProjectSerialization::saveProject(config, projectFile.string()));
    }

    // What Project Settings > Save does (ProjectSettingsWindow::saveToProject): send back the config
    // it read, then save in place.
    void saveLikeProjectSettings()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        const auto project = dispatcher.query(events::project::GetCurrentProjectQuery{});
        REQUIRE(project.has_value());

        events::project::UpdateProjectConfigCommand update;
        update.config = *project;
        REQUIRE(dispatcher.execute(update));

        events::project::SaveProjectCommand save;
        save.filePath = "";
        REQUIRE(dispatcher.execute(save));
    }
}

TEST_SUITE("ProjectService")
{
    TEST_CASE("SaveProjectCommand with empty path saves to current project path")
    {
        DispatcherScope dispatcherScope;
        const auto directory = makeTestDirectory("VertexForge_ProjectService_SaveInPlace");
        const auto projectPath = directory / "ProjectServiceTest.vfproject";

        auto config = makeValidProjectConfig(directory);
        REQUIRE(serialization::ProjectSerialization::saveProject(config, projectPath.string()));

        services::ProjectServiceImpl service;
        service.registerEventHandlers();
        REQUIRE(service.loadProject(projectPath.string()));

        auto updatedConfig = *service.getCurrentProject();
        updatedConfig.startupScene = "UpdatedStartup.vfScene";

        events::project::UpdateProjectConfigCommand updateCommand;
        updateCommand.config = updatedConfig;
        REQUIRE(events::EventDispatcher::instance().execute(updateCommand));

        events::project::SaveProjectCommand saveCommand;
        saveCommand.filePath = "";
        CHECK(events::EventDispatcher::instance().execute(saveCommand));

        auto pathAfterSave = events::EventDispatcher::instance().query(events::project::GetProjectPathQuery{});
        REQUIRE(pathAfterSave.has_value());
        CHECK(*pathAfterSave == projectPath.string());

        auto savedProject = serialization::ProjectSerialization::loadProject(projectPath.string());
        REQUIRE(savedProject.has_value());
        CHECK(savedProject->startupScene == "UpdatedStartup.vfScene");

        std::filesystem::remove_all(directory);
    }

    TEST_CASE("SaveProjectCommand with empty path fails for unsaved project")
    {
        DispatcherScope dispatcherScope;
        const auto directory = makeTestDirectory("VertexForge_ProjectService_Unsaved");

        services::ProjectServiceImpl service;
        service.registerEventHandlers();
        REQUIRE(service.newProject(makeValidProjectConfig(directory)));

        events::project::SaveProjectCommand saveCommand;
        saveCommand.filePath = "";
        CHECK_FALSE(events::EventDispatcher::instance().execute(saveCommand));
        CHECK_FALSE(service.getProjectPath().has_value());

        std::filesystem::remove_all(directory);
    }

    TEST_CASE("successful project config updates publish the updated project")
    {
        DispatcherScope dispatcherScope;
        const auto directory = makeTestDirectory("VertexForge_ProjectService_ConfigUpdated");

        services::ProjectServiceImpl service;
        auto projectConfig = makeValidProjectConfig(directory);

        int notificationCount = 0;
        config::ProjectConfig notifiedProject;
        auto token = events::EventDispatcher::instance().subscribe<
            events::project::ProjectConfigUpdatedNotification>(
            [&](const events::project::ProjectConfigUpdatedNotification& notification)
            {
                ++notificationCount;
                notifiedProject = notification.project;
            });

        auto invalid = projectConfig;
        invalid.projectName.clear();
        CHECK_FALSE(service.updateProjectConfig(invalid));
        CHECK(notificationCount == 0);

        projectConfig.fontFallbackChain = {"Assets/Fonts/Primary.vfFont",
                                           "Assets/Fonts/Emoji.vfFont"};
        REQUIRE(service.updateProjectConfig(projectConfig));
        CHECK(notificationCount == 1);
        CHECK(notifiedProject.fontFallbackChain == projectConfig.fontFallbackChain);

        events::EventDispatcher::instance().unsubscribe(token);
        std::filesystem::remove_all(directory);
    }

    // VK-1682: the editor's open guard moves its project lock; a refusal must leave the open project alone.
    TEST_CASE("a refusing open guard leaves the current project and publishes nothing")
    {
        DispatcherScope dispatcherScope;
        const auto directoryA = makeTestDirectory("VertexForge_ProjectService_GuardA");
        const auto directoryB = makeTestDirectory("VertexForge_ProjectService_GuardB");
        const auto projectA = directoryA / "A.vfproj";
        const auto projectB = directoryB / "B.vfproj";
        REQUIRE(serialization::ProjectSerialization::saveProject(makeValidProjectConfig(directoryA), projectA.string()));
        auto configB = makeValidProjectConfig(directoryB);
        configB.projectName = "GuardB";
        REQUIRE(serialization::ProjectSerialization::saveProject(configB, projectB.string()));

        services::ProjectServiceImpl service;
        service.registerEventHandlers();

        std::vector<std::filesystem::path> asked;
        service.setOpenGuard([&asked, projectB](const std::filesystem::path& projectFile) -> std::optional<std::string>
        {
            asked.push_back(projectFile);
            if (projectFile == projectB)
            {
                return std::string("another editor has it open");
            }
            return std::nullopt;
        });

        REQUIRE(service.loadProject(projectA.string()));
        REQUIRE(asked.size() == 1);
        CHECK(asked[0] == projectA);

        int loadedNotifications = 0;
        auto token = events::EventDispatcher::instance().subscribe<events::project::ProjectLoadedNotification>(
            [&loadedNotifications](const events::project::ProjectLoadedNotification&)
            {
                ++loadedNotifications;
            });

        CHECK_FALSE(service.loadProject(projectB.string()));
        CHECK(asked.size() == 2);
        CHECK(loadedNotifications == 0);
        REQUIRE(service.getCurrentProject().has_value());
        CHECK(service.getCurrentProject()->projectName == "ProjectServiceTest");
        REQUIRE(service.getProjectPath().has_value());
        CHECK(*service.getProjectPath() == projectA.string());

        events::EventDispatcher::instance().unsubscribe(token);
        std::filesystem::remove_all(directoryA);
        std::filesystem::remove_all(directoryB);
    }

    TEST_CASE("the open guard is not asked about a project that fails to parse")
    {
        DispatcherScope dispatcherScope;
        const auto directory = makeTestDirectory("VertexForge_ProjectService_GuardParse");
        const auto broken = directory / "Broken.vfproj";
        {
            std::ofstream out(broken, std::ios::binary);
            out << "{ not json";
        }

        services::ProjectServiceImpl service;
        int guardCalls = 0;
        service.setOpenGuard([&guardCalls](const std::filesystem::path&) -> std::optional<std::string>
        {
            ++guardCalls;
            return std::nullopt;
        });

        CHECK_FALSE(service.loadProject(broken.string()));
        CHECK(guardCalls == 0);
        CHECK_FALSE(service.isProjectLoaded());

        std::filesystem::remove_all(directory);
    }

    // VK-1687: the .vfproj keeps the authored working directory; memory holds the resolved one.
    TEST_CASE("a relative working directory survives Project Settings Save and a folder move")
    {
        namespace fs = std::filesystem;
        DispatcherScope dispatcherScope;
        const auto root = makeTestDirectory("VertexForge_ProjectService_Portable");
        const auto original = root / "VK1687OriginalFolder";
        const auto moved = root / "VK1687MovedFolder";
        fs::create_directories(original / "scenes");
        {
            std::ofstream scene(original / "scenes" / "Main.vfScene", std::ios::binary);
            scene << "{}";
        }
        writeProject(original / "Game.vfproj", ".");

        services::ProjectServiceImpl service;
        service.registerEventHandlers();
        REQUIRE(service.loadProject((original / "Game.vfproj").string()));

        std::error_code ec;
        const std::string resolved = service.getCurrentProject()->workingDirectory;
        CHECK(fs::path(resolved).is_absolute());
        CHECK(fs::equivalent(resolved, original, ec));

        saveLikeProjectSettings();
        CHECK(savedWorkingDirectory(original / "Game.vfproj") == ".");
        CHECK(readText(original / "Game.vfproj").find("VK1687OriginalFolder") == std::string::npos);
        // Memory still holds the resolved path every reader joins onto.
        CHECK(service.getCurrentProject()->workingDirectory == resolved);

        fs::rename(original, moved);
        REQUIRE(service.loadProject((moved / "Game.vfproj").string()));
        const auto reopened = *service.getCurrentProject();
        CHECK(fs::equivalent(reopened.workingDirectory, moved, ec));
        CHECK(fs::exists(fs::path(reopened.workingDirectory) / "scenes" / "Main.vfScene"));

        saveLikeProjectSettings();
        CHECK(savedWorkingDirectory(moved / "Game.vfproj") == ".");

        fs::remove_all(root, ec);
    }

    TEST_CASE("an absolute working directory inside the project is saved relative")
    {
        namespace fs = std::filesystem;
        DispatcherScope dispatcherScope;
        const auto directory = makeTestDirectory("VertexForge_ProjectService_Repair");
        fs::create_directories(directory / "Assets");

        services::ProjectServiceImpl service;
        service.registerEventHandlers();

        // What an older editor wrote after one Project Settings Save: its own folder, absolute.
        writeProject(directory / "Self.vfproj", directory.string());
        REQUIRE(service.loadProject((directory / "Self.vfproj").string()));
        saveLikeProjectSettings();
        CHECK(savedWorkingDirectory(directory / "Self.vfproj") == ".");

        writeProject(directory / "Assets.vfproj", (directory / "Assets").string());
        REQUIRE(service.loadProject((directory / "Assets.vfproj").string()));
        saveLikeProjectSettings();
        CHECK(savedWorkingDirectory(directory / "Assets.vfproj") == "Assets");

        // A folder that does not exist, written with a trailing separator, is never saved as "".
        const std::string missing =
            (directory / "Missing").string() + std::string(1, static_cast<char>(fs::path::preferred_separator));
        writeProject(directory / "Missing.vfproj", missing);
        REQUIRE(service.loadProject((directory / "Missing.vfproj").string()));
        saveLikeProjectSettings();
        CHECK(savedWorkingDirectory(directory / "Missing.vfproj") == "Missing");

        std::error_code ec;
        fs::remove_all(directory, ec);
    }

    TEST_CASE("working directories outside the project are kept exactly as written")
    {
        namespace fs = std::filesystem;
        DispatcherScope dispatcherScope;
        const auto directory = makeTestDirectory("VertexForge_ProjectService_Outside");
        const auto shared = makeTestDirectory("VertexForge_ProjectService_OutsideShared");

        services::ProjectServiceImpl service;
        service.registerEventHandlers();

        // Forward slashes on purpose: the authored spelling, not a normalised one, comes back.
        const std::string absoluteOutside = shared.generic_string();
        writeProject(directory / "Absolute.vfproj", absoluteOutside);
        REQUIRE(service.loadProject((directory / "Absolute.vfproj").string()));
        saveLikeProjectSettings();
        CHECK(savedWorkingDirectory(directory / "Absolute.vfproj") == absoluteOutside);

        const std::string relativeOutside = "../VertexForge_ProjectService_OutsideShared";
        writeProject(directory / "Relative.vfproj", relativeOutside);
        REQUIRE(service.loadProject((directory / "Relative.vfproj").string()));
        std::error_code ec;
        CHECK(fs::equivalent(service.getCurrentProject()->workingDirectory, shared, ec));
        saveLikeProjectSettings();
        CHECK(savedWorkingDirectory(directory / "Relative.vfproj") == relativeOutside);

        fs::remove_all(directory, ec);
        fs::remove_all(shared, ec);
    }

    TEST_CASE("the authored working directory follows updates, Save As and refused switches")
    {
        namespace fs = std::filesystem;
        DispatcherScope dispatcherScope;
        const auto directory = makeTestDirectory("VertexForge_ProjectService_Authored");
        const auto other = makeTestDirectory("VertexForge_ProjectService_AuthoredOther");
        fs::create_directories(directory / "Assets");
        fs::create_directories(directory / "Content");
        std::error_code ec;

        services::ProjectServiceImpl service;
        service.registerEventHandlers();
        auto& dispatcher = events::EventDispatcher::instance();

        // A spelling only rule 1 (written verbatim) keeps; anything recomputed would give "Assets".
        writeProject(directory / "Game.vfproj", "./Assets");
        writeProject(other / "Other.vfproj", "Elsewhere");
        service.setOpenGuard([other](const fs::path& projectFile) -> std::optional<std::string>
        {
            if (projectFile.parent_path() == other)
            {
                return std::string("refused");
            }
            return std::nullopt;
        });

        REQUIRE(service.loadProject((directory / "Game.vfproj").string()));
        CHECK_FALSE(service.loadProject((other / "Other.vfproj").string()));
        saveLikeProjectSettings();
        CHECK(savedWorkingDirectory(directory / "Game.vfproj") == "./Assets");

        // A new relative value is resolved against the project folder and written as given.
        auto updated = *service.getCurrentProject();
        updated.workingDirectory = "Content";
        events::project::UpdateProjectConfigCommand update;
        update.config = updated;
        REQUIRE(dispatcher.execute(update));
        CHECK(fs::path(service.getCurrentProject()->workingDirectory).is_absolute());
        CHECK(fs::equivalent(service.getCurrentProject()->workingDirectory, directory / "Content", ec));
        saveLikeProjectSettings();
        CHECK(savedWorkingDirectory(directory / "Game.vfproj") == "Content");

        // Save As into a folder that does not contain the working directory: it must still name it.
        events::project::SaveProjectCommand saveAs;
        saveAs.filePath = (other / "Copy.vfproj").string();
        REQUIRE(dispatcher.execute(saveAs));
        const std::string copied = savedWorkingDirectory(other / "Copy.vfproj");
        CHECK(fs::path(copied).is_absolute());
        CHECK(fs::equivalent(copied, directory / "Content", ec));

        fs::remove_all(directory, ec);
        fs::remove_all(other, ec);
    }
}
