#pragma once
#include "../../utilities/config/ProjectConfig.hpp"
#include <optional>
#include <string>

namespace services {

    class IProjectService {
    public:
        virtual ~IProjectService() = default;

        virtual void registerEventHandlers() = 0;

        virtual bool loadProject(const std::string& filePath) = 0;
        virtual bool saveProject(const std::string& filePath) = 0;
        virtual bool saveProject() = 0;
        virtual bool newProject(const config::ProjectConfig& config) = 0;
        virtual bool updateProjectConfig(const config::ProjectConfig& config) = 0;

        virtual std::optional<config::ProjectConfig> getCurrentProject() const = 0;
        virtual std::optional<std::string> getProjectPath() const = 0;
        virtual bool isProjectLoaded() const = 0;
    };

}
