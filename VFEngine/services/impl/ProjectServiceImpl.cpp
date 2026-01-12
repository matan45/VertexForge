#include "ProjectServiceImpl.hpp"
#include "../events/ProjectEvents.hpp"
#include "serialization/ProjectSerialization.hpp"
#include "print/EditorLogger.hpp"

namespace services
{
    void ProjectServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::project::LoadProjectCommand>(
            [this](const events::project::LoadProjectCommand& cmd)
            {
                return loadProject(cmd.filePath);
            });

        dispatcher.registerCommandHandler<events::project::SaveProjectCommand>(
            [this](const events::project::SaveProjectCommand& cmd)
            {
                return saveProject(cmd.filePath);
            });

        dispatcher.registerCommandHandler<events::project::NewProjectCommand>(
            [this](const events::project::NewProjectCommand& cmd)
            {
                return newProject(cmd.config);
            });

        dispatcher.registerCommandHandler<events::project::UpdateProjectConfigCommand>(
            [this](const events::project::UpdateProjectConfigCommand& cmd)
            {
                return updateProjectConfig(cmd.config);
            });

        dispatcher.registerQueryHandler<events::project::GetCurrentProjectQuery>(
            [this](const events::project::GetCurrentProjectQuery&)
            {
                return getCurrentProject();
            });

        dispatcher.registerQueryHandler<events::project::GetProjectPathQuery>(
            [this](const events::project::GetProjectPathQuery&)
            {
                return getProjectPath();
            });

        dispatcher.registerQueryHandler<events::project::IsProjectLoadedQuery>(
            [this](const events::project::IsProjectLoadedQuery&)
            {
                return isProjectLoaded();
            });
    }

    bool ProjectServiceImpl::loadProject(const std::string& filePath)
    {
        auto project = serialization::ProjectSerialization::loadProject(filePath);
        if (!project)
        {
            return false;
        }

        currentProject = *project;
        currentProjectPath = filePath;

        events::project::ProjectLoadedNotification notification;
        notification.project = *project;
        notification.filePath = filePath;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    bool ProjectServiceImpl::saveProject(const std::string& filePath)
    {
        if (!currentProject)
        {
            vfLogError("No project loaded to save");
            return false;
        }

        if (!serialization::ProjectSerialization::saveProject(*currentProject, filePath))
        {
            return false;
        }

        currentProjectPath = filePath;

        events::project::ProjectSavedNotification notification;
        notification.filePath = filePath;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    bool ProjectServiceImpl::saveProject()
    {
        if (!currentProjectPath)
        {
            vfLogError("No project path set, use saveProject(filePath) instead");
            return false;
        }

        return saveProject(*currentProjectPath);
    }

    bool ProjectServiceImpl::newProject(const config::ProjectConfig& config)
    {
        if (!config.isValid())
        {
            vfLogError("Invalid project configuration");
            return false;
        }

        if (currentProject)
        {
            events::project::ProjectClosedNotification closedNotification;
            events::EventDispatcher::instance().publish(closedNotification);
        }

        currentProject = config;
        currentProjectPath = std::nullopt;

        events::project::ProjectLoadedNotification notification;
        notification.project = config;
        notification.filePath = "";
        events::EventDispatcher::instance().publish(notification);

        vfLogInfo("New project created: {}", config.projectName);
        return true;
    }

    std::optional<config::ProjectConfig> ProjectServiceImpl::getCurrentProject() const
    {
        return currentProject;
    }

    std::optional<std::string> ProjectServiceImpl::getProjectPath() const
    {
        return currentProjectPath;
    }

    bool ProjectServiceImpl::isProjectLoaded() const
    {
        return currentProject.has_value();
    }

    bool ProjectServiceImpl::updateProjectConfig(const config::ProjectConfig& config)
    {
        if (!config.isValid())
        {
            vfLogError("Invalid project configuration");
            return false;
        }

        currentProject = config;
        return true;
    }
}
