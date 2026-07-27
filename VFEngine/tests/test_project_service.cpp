#include <doctest.h>

#include "events/EventDispatcher.hpp"
#include "events/project/ProjectEvents.hpp"
#include "impl/project/ProjectServiceImpl.hpp"
#include "serialization/ProjectSerialization.hpp"

#include <filesystem>
#include <string>

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
}
