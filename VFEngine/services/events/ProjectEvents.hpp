#pragma once
#include "EventTypes.hpp"
#include "../../utilities/config/ProjectConfig.hpp"
#include <optional>
#include <string>

namespace events::project {

    struct LoadProjectCommand : ICommand<bool> {
        std::string filePath;

        std::string_view getName() const override { return "LoadProject"; }
    };

    struct SaveProjectCommand : ICommand<bool> {
        std::string filePath;

        std::string_view getName() const override { return "SaveProject"; }
    };

    struct NewProjectCommand : ICommand<bool> {
        config::ProjectConfig config;

        std::string_view getName() const override { return "NewProject"; }
    };

    struct GetCurrentProjectQuery : IQuery<std::optional<config::ProjectConfig>> {
        std::string_view getName() const override { return "GetCurrentProject"; }
    };

    struct GetProjectPathQuery : IQuery<std::optional<std::string>> {
        std::string_view getName() const override { return "GetProjectPath"; }
    };

    struct IsProjectLoadedQuery : IQuery<bool> {
        std::string_view getName() const override { return "IsProjectLoaded"; }
    };

    struct ProjectLoadedNotification : INotification {
        config::ProjectConfig project;
        std::string filePath;

        std::string_view getName() const override { return "ProjectLoaded"; }
    };

    struct ProjectSavedNotification : INotification {
        std::string filePath;

        std::string_view getName() const override { return "ProjectSaved"; }
    };

    struct ProjectClosedNotification : INotification {
        std::string_view getName() const override { return "ProjectClosed"; }
    };

}
