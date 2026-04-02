#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "data/DTOs.hpp"
#include "data/AsyncLoadingTypes.hpp"
#include "resource/Types.hpp"
#include <string>
#include <future>

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

        // Channel view: 0=RGBA, 1=R, 2=G, 3=B, 4=A
        int channelView = 0;
        services::EditorTextureHandle channelHandle;
        std::future<std::shared_ptr<resource::TextureData>> channelFuture;
        int pendingChannel = 0;

    public:
        explicit ImagePreviewWindow(const std::string& filePath, bool hdr = false);
        ~ImagePreviewWindow() override;

        void draw() override;

        bool shouldClose() const override { return !isOpen; }

    private:
        void loadImageAsync();
        void updateAsyncLoading();
        void updateChannelLoading();
        void drawImagePanel();
        void drawInfoPanel();
        void drawLoadingIndicator(float width, float height);
        void startChannelBuild(int channel);
        void releaseChannelTexture();
    };
}
