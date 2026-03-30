#pragma once
#include "nfd/FileDialog.hpp"
#include <string>

namespace windows
{
    class ProjectSettingsWindow
    {
    private:
        bool visible = false;
        nfd::FileDialog fileDialog;

        std::string projectName;
        std::string version;
        std::string workingDirectory;
        std::string startupScene;
        std::string exeIconPath;

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
