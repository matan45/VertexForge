#pragma once
#include "nfd/FileDialog.hpp"
#include "ui/UITheme.hpp"
#include <string>

namespace windows
{
    // Minimal .vfTheme authoring tool: open/create styles, edit their
    // color/float/asset properties, save, and re-apply to the open scene.
    class ThemeEditorWindow
    {
    private:
        bool visible = false;
        nfd::FileDialog fileDialog;

        std::string themePath;
        utilities::ui::UITheme theme;
        bool dirty = false;

        std::string selectedStyle;
        char newStyleName[128] = {};
        char newPropertyName[128] = {};
        int newPropertyKind = 0; // 0=color, 1=float, 2=asset

    public:
        void draw();
        void show();

    private:
        void drawToolbar();
        void drawStyleList();
        void drawStyleProperties();
        void openTheme(const std::string& path);
        void saveTheme();
        void applyToScene();
    };
}
