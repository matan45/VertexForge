#pragma once
#include "../../interfaces/project/IProjectService.hpp"
#include "../../events/EventDispatcher.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace services
{
    class ProjectServiceImpl : public IProjectService
    {
    public:
        // VK-1682: asked right before a successfully parsed project becomes the current one
        // (nothing after that point can fail). Returns why the switch is refused, or nullopt to
        // allow it. The editor uses it to move its .vertexforge/editor.lock to the new project;
        // the Runtime sets none.
        using OpenGuard = std::function<std::optional<std::string>(const std::filesystem::path& projectFile)>;

    private:
        std::optional<config::ProjectConfig> currentProject;
        std::optional<std::string> currentProjectPath;
        OpenGuard openGuard;

    public:
        ProjectServiceImpl() = default;
        ~ProjectServiceImpl() override = default;

        void setOpenGuard(OpenGuard guard);

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
