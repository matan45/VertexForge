#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "data/DTOs.hpp"
#include "data/AsyncLoadingTypes.hpp"
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

        // Async loading state
        services::TextureLoadingProgress loadingProgress;

        // Zoom/pan state
        float zoom = 1.0f;
        float panX = 0.0f;
        float panY = 0.0f;

        int selectedMipLevel = 0;

    public:
        explicit ImagePreviewWindow(const std::string& filePath, bool hdr = false);
        ~ImagePreviewWindow() override;

        void draw() override;

        bool shouldClose() const override { return !isOpen; }
        const std::string& getImagePath() const { return imagePath; }

    private:
        void loadImageAsync();
        void updateAsyncLoading();
        void drawImagePanel();
        void drawInfoPanel();
        void drawLoadingIndicator(float width, float height);
    };
}
