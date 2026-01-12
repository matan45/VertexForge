#pragma once
#include "../interfaces/IProjectService.hpp"
#include "../events/EventDispatcher.hpp"
#include <optional>
#include <string>

namespace services
{
    class ProjectServiceImpl : public IProjectService
    {
    private:
        std::optional<config::ProjectConfig> currentProject;
        std::optional<std::string> currentProjectPath;

    public:
        ProjectServiceImpl() = default;
        ~ProjectServiceImpl() override = default;

        void registerEventHandlers() override;

        bool loadProject(const std::string& filePath) override;
        bool saveProject(const std::string& filePath) override;
        bool saveProject() override;
        bool newProject(const config::ProjectConfig& config) override;
        bool updateProjectConfig(const config::ProjectConfig& config) override;

        std::optional<config::ProjectConfig> getCurrentProject() const override;
        std::optional<std::string> getProjectPath() const override;
        bool isProjectLoaded() const override;
    };
}
