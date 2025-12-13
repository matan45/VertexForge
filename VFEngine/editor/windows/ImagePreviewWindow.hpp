#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "data/DTOs.hpp"
#include <string>

namespace windows
{
    class ImagePreviewWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::string imagePath;
        std::string windowTitle;
        bool isHDR = false;

        // Texture handle from editor texture service
        services::EditorTextureHandle imageHandle;

        // Window state
        bool isOpen = true;
        bool needsInit = true;

        // Zoom/pan state
        float zoom = 1.0f;
        float panX = 0.0f;
        float panY = 0.0f;

    public:
        explicit ImagePreviewWindow(const std::string& filePath, bool hdr = false);
        ~ImagePreviewWindow() override;

        void draw() override;

        bool shouldClose() const override { return !isOpen; }
        const std::string& getImagePath() const { return imagePath; }

    private:
        void loadImage();
        void drawImagePanel();
        void drawInfoPanel();
    };
}
