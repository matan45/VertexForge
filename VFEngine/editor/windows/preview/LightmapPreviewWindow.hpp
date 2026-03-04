#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "data/DTOs.hpp"
#include "resource/Types.hpp"
#include <string>

namespace windows
{
    class LightmapPreviewWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::string lightmapPath;
        std::string windowTitle;

        resource::LightmapData lightmapData;
        services::EditorTextureHandle textureHandle;

        bool isOpen = true;
        bool needsLoad = true;
        bool loadFailed = false;

        float zoom = 1.0f;
        float exposure = 1.0f;

    public:
        explicit LightmapPreviewWindow(const std::string& filePath);
        ~LightmapPreviewWindow() override;

        void draw() override;

        bool shouldClose() const override { return !isOpen; }

    private:
        void loadLightmap();
        void drawImagePanel();
        void drawInfoPanel();
    };
}
