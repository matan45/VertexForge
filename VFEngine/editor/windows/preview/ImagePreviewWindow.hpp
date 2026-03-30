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
        
        services::EditorTextureHandle imageHandle;
        
        bool isOpen = true;
        bool needsInit = true;
        
        services::TextureLoadingProgress loadingProgress;
        
        float zoom = 1.0f;

        int selectedMipLevel = 0;

    public:
        explicit ImagePreviewWindow(const std::string& filePath, bool hdr = false);
        ~ImagePreviewWindow() override;

        void draw() override;

        bool shouldClose() const override { return !isOpen; }

    private:
        void loadImageAsync();
        void updateAsyncLoading();
        void drawImagePanel();
        void drawInfoPanel();
        void drawLoadingIndicator(float width, float height);
    };
}
