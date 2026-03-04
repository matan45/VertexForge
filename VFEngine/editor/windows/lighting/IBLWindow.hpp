#pragma once
#include "nfd/FileDialog.hpp"
#include "data/DTOs.hpp"
#include <filesystem>

namespace fs = std::filesystem;

namespace windows
{
    class IBLWindow
    {
    private:
        bool visible = false;
        nfd::FileDialog fileDialog;
        fs::path selectedIBLFile;
        services::EditorTextureHandle iblPreviewHandle;

    public:
        void draw();

        void show() { visible = true; }
        void hide() { visible = false; }
        bool isVisible() const { return visible; }

        // Called when scene is cleared to clean up resources
        void onSceneCleared();
    };
}
