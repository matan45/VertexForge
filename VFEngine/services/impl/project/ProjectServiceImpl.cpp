#include "print/Log.hpp"
#include "ProjectServiceImpl.hpp"
#include "../../events/project/ProjectEvents.hpp"
#include "serialization/ProjectSerialization.hpp"
#include <filesystem>
#include <system_error>

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
                if (cmd.filePath.empty())
                {
                    return saveProject();
                }

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

    void ProjectServiceImpl::setOpenGuard(OpenGuard guard)
    {
        openGuard = std::move(guard);
    }

    bool ProjectServiceImpl::loadProject(const std::string& filePath)
    {
        auto project = serialization::ProjectSerialization::loadProject(filePath);
        if (!project)
        {
            return false;
        }

        // Path work can throw (a path the ANSI code page cannot represent, a failing file
        // system); a failure here must leave the current project as it was.
        try
        {
            // Resolve workingDirectory relative to the project file's directory
            std::filesystem::path projectFilePath(filePath);
            std::filesystem::path projectDir = projectFilePath.parent_path();
            std::filesystem::path workingDir(project->workingDirectory);

            if (workingDir.is_relative())
            {
                project->workingDirectory = (projectDir / workingDir).lexically_normal().string();
            }

            // Validate paths after resolution
            std::error_code ec;
            if (!std::filesystem::exists(project->workingDirectory, ec))
            {
                vfLogWarning("Project working directory does not exist: {}", project->workingDirectory);
            }

            std::filesystem::path scenePath = std::filesystem::path(project->workingDirectory) / project->startupScene;
            if (!std::filesystem::exists(scenePath, ec))
            {
                vfLogWarning("Project startup scene not found: {}", scenePath.string());
            }

            // VK-1682: last, so an accepted switch (the editor lock already moved) is always
            // followed by the commit below: only assignments and publish (which catches every
            // subscriber's exception) remain.
            if (openGuard)
            {
                if (std::optional<std::string> refusal = openGuard(projectFilePath))
                {
                    vfLogError("Cannot open project {}: {}", filePath, *refusal);
                    return false;
                }
            }
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to open project {}: {}", filePath, e.what());
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

        if (filePath.empty())
        {
            vfLogError("Project file path is empty");
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
        if (!currentProjectPath || currentProjectPath->empty())
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

        events::project::ProjectConfigUpdatedNotification notification;
        notification.project = config;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }
}
