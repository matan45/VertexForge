#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "../../graphics/render/svt/SVTFileFormat.hpp"
#include "data/DTOs.hpp"
#include "data/AsyncLoadingTypes.hpp"
#include <string>
#include <vector>
#include <imgui.h>

namespace windows
{
    // Preview window for .vfSVT (Sparse Virtual Texture) files.
    // Shows file info, tile grid visualization with actual texture content.
    class SVTPreviewWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::string filePath;
        std::string windowTitle;

        render::svt::SVTFileReader reader;
        bool loaded = false;
        bool isOpen = true;

        // Stats
        uint32_t totalTiles = 0;
        uint32_t presentTiles = 0;
        uint64_t fileSizeBytes = 0;

        // View state
        int selectedMipLevel = 0;
        float gridZoom = 1.0f;

        // Full mip texture preview (loaded via editor texture system)
        services::EditorTextureHandle mipPreviewHandle;
        bool mipPreviewLoading = false;
        int loadedPreviewMip = -1;
        services::TextureLoadingProgress loadingProgress;

        // Assembled mip image for preview
        std::vector<uint8_t> assembledMipRGBA;
        uint32_t assembledWidth = 0;
        uint32_t assembledHeight = 0;

    public:
        explicit SVTPreviewWindow(const std::string& path);
        ~SVTPreviewWindow() override;

        void draw() override;
        bool shouldClose() const override { return !isOpen; }

    private:
        void loadFile();
        void drawInfoPanel();
        void drawPreviewPanel();
        void countPresentTiles();
        void loadMipPreview(int mipLevel);
        void decodeTilesForMip(int mipLevel, uint32_t downsample);
        void uploadPreviewTexture();
    };
}
