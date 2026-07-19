#pragma once
#include "nfd/FileDialog.hpp"
#include "data/DTOs.hpp"
#include <filesystem>
#include <string>

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
        // VK-1574: live IBL knobs (mirror the scene IBLComponent).
        float iblIntensity = 1.0f;
        float iblRotationDeg = 0.0f;
        float iblTint[3] = {1.0f, 1.0f, 1.0f};

        // VK-1574: single apply path — writes the component, drives the renderer,
        // and pushes live params. `bake` re-runs the (blocking today) HDR bake; the
        // knob sliders call with bake=false so they update ambient without a re-bake.
        void applyEnvironment(services::EntityHandle rootHandle, const std::string& path, bool bake);

    public:
        void draw();

        void show() { visible = true; }
        void hide() { visible = false; }
        bool isVisible() const { return visible; }

        // Called when scene is cleared to clean up resources
        void onSceneCleared();
    };
}
