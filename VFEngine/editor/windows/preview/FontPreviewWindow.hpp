#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "PreviewWindowChrome.hpp"
#include "resource/Types.hpp"
#include "data/DTOs.hpp"
#include "events/EventTypes.hpp"
#include <imgui.h>
#include <string>
#include <future>
#include <atomic>

namespace windows
{
    struct FontLoadResult
    {
        bool success = false;
        std::string errorMessage;
        resource::FontData fontData;
        resource::TextureData atlasAsRGBA;
    };

    class FontPreviewWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::string fontPath;
        std::string windowTitle;

        resource::FontData fontData;
        services::EditorTextureHandle atlasHandle;

        bool isOpen = true;
        bool needsInit = true;

        editor::preview::WindowMaximizer maximizer;
        ImVec2 initialSize{0.0f, 0.0f};
        bool sizeSaved = false;

        std::future<FontLoadResult> loadFuture;
        std::atomic<bool> loadingInProgress{false};
        std::atomic<bool> loadingCancelled{false};
        bool fontLoaded = false;
        bool loadFailed = false;
        std::string errorMessage;

        // VK-1629: a reimport rewrites this .vfFont in place. The notification
        // arrives on the import worker thread, so it only raises this flag; draw()
        // does the actual reload on the UI thread (releasing the atlas descriptor
        // and re-reading the file are both UI-thread-only operations).
        events::SubscriptionToken importCompletedToken;
        std::atomic<bool> reloadRequested{false};

        // Preview settings
        char textInputBuffer[1024] = {};
        float previewFontSize = 32.0f;
        float atlasZoom = 1.0f;
        bool showCharacterGrid = false;
        bool showAtlasPreview = false;

    public:
        explicit FontPreviewWindow(const std::string& filePath);
        ~FontPreviewWindow() override;
        void draw() override;
        bool shouldClose() const override { return !isOpen; }

    private:
        void startAsyncLoad();
        void updateAsyncLoading();
        void reloadFromDisk();
        FontLoadResult loadFontBackground(const std::string& path);

        void drawInfoPanel();
        void drawPreviewPanel();
        void drawTextPreview();
        void drawCharacterGrid();
        void drawAtlasPreview();
        void drawLoadingIndicator();

        void renderTextWithGlyphs(const std::string& text, float fontSize, ImVec2 startPos);
    };
}
