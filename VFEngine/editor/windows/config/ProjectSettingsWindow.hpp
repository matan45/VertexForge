#pragma once
#include "nfd/FileDialog.hpp"
#include <string>
#include <vector>

namespace windows
{
    class ProjectSettingsWindow
    {
    private:
        bool visible = false;
        nfd::FileDialog fileDialog;

        std::string projectName;
        std::string version;
        std::string publisher;  // VK-1726: names the shipped game's per-user state folder
        std::string workingDirectory;
        std::string startupScene;
        std::string exeIconPath;
        std::vector<std::string> fontFallbackChain;
        // VK-1687: why the last Browse for a startup scene was refused (it lay outside the project).
        std::string startupSceneError;

        bool isDirty = false;
        bool hasProject = false;

        void loadFromProject();
        void saveToProject();
        bool validateStartupScene() const;
        bool validateExeIcon() const;

    public:
        void draw();
        void drawContent();
        void show();
        void hide() { visible = false; }
        bool isVisible() const { return visible; }
    };
}
