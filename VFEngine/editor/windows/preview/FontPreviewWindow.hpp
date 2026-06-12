#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "PreviewWindowChrome.hpp"
#include "resource/Types.hpp"
#include "data/DTOs.hpp"
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
        FontLoadResult loadFontBackground(const std::string& path);
        static resource::TextureData convertAtlasToRGBA(const resource::FontAtlasData& atlas);

        void drawInfoPanel();
        void drawPreviewPanel();
        void drawTextPreview();
        void drawCharacterGrid();
        void drawAtlasPreview();
        void drawLoadingIndicator();

        void renderTextWithGlyphs(const std::string& text, float fontSize, ImVec2 startPos);
    };
}
