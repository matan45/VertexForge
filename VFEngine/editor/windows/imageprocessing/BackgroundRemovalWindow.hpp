#pragma once
#include "nfd/FileDialog.hpp"
#include "events/EventDispatcher.hpp"
#include "data/DTOs.hpp"
#include "processor/BackgroundRemover.hpp"
#include <string>
#include <vector>
#include <future>
#include <atomic>

namespace windows
{
    class BackgroundRemovalWindow
    {
    private:
        bool visible = false;
        nfd::FileDialog fileDialog;

        // Input image
        std::string inputPath;
        std::vector<uint8_t> sourcePixels; // RGBA8 of loaded image
        uint32_t sourceWidth = 0;
        uint32_t sourceHeight = 0;

        // Processing parameters
        imageprocessing::RemovalParams params;
        int methodIndex = 0; // 0=Color, 1=Luminance, 2=EdgeAware

        // Preview state
        services::EditorTextureHandle originalHandle;
        services::EditorTextureHandle resultHandle;
        bool processDirty = false;
        float lastProcessTime = 0.0f;
        static constexpr float processDebounceTime = 0.3f;

        // Result
        imageprocessing::RemovalResult lastResult;

        // Export state
        std::string exportPath;
        bool exporting = false;
        std::future<bool> exportFuture;
        std::string exportStatusMessage;

    public:
        ~BackgroundRemovalWindow();

        void draw();
        void show();
        void showWithFile(const std::string& filePath);

    private:
        void drawControls();
        void drawPreview();
        void drawExportSection();
        void loadInputImage(const std::string& path);
        void processImage();
        void uploadOriginalToGPU();
        void uploadResultToGPU();
        void releaseTextures();
        void startExport();
        void pollExport();
    };
}
