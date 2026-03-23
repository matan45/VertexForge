#pragma once
#include "nfd/FileDialog.hpp"
#include "events/EventDispatcher.hpp"
#include "data/DTOs.hpp"
#include "generator/HeightmapGenerator.hpp"
#include <string>
#include <future>
#include <atomic>

namespace windows
{
    class HeightmapGeneratorWindow
    {
    private:
        bool visible = false;
        nfd::FileDialog fileDialog;

        // Generation parameters
        procedural::HeightmapParams params;
        int resolutionIndex = 2;   // 0=1024, 1=2048, 2=4096, 3=8192, 4=16384
        int noiseTypeIndex = 0;    // 0=Perlin, 1=Simplex
        int fractalTypeIndex = 1;  // 0=None, 1=FBM, 2=Ridged

        // Presets
        int presetIndex = 0;

        // Export settings
        int exportFormatIndex = 0; // 0=Uncompressed .vfImage, 1=BC7 .vfImage, 2=.vfSVT, 3=Both
        std::string exportPath;

        // Preview state
        services::EditorTextureHandle previewHandle;
        bool previewDirty = true;
        float lastPreviewTime = 0.0f;
        static constexpr float previewDebounceTime = 0.2f;

        // Generation state
        bool generating = false;
        std::atomic<float> generationProgress{0.0f};
        std::future<procedural::HeightmapResult> generationFuture;
        procedural::HeightmapResult lastResult;

        // Export state
        bool exporting = false;
        std::future<bool> exportFuture;
        std::string pendingExportPath;
        std::string exportStatusMessage;

    public:
        ~HeightmapGeneratorWindow();

        void draw();
        void show();

    private:
        void drawParameterControls();
        void drawPreview();
        void drawExportSection();
        void updatePreview();
        void releasePreviewTexture();
        void startGeneration();
        void pollGeneration();
        void pollExport();
        void startExport();
        void syncParamsFromUI();
        void applyPreset(int preset);
    };
}
