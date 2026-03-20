#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "../../graphics/render/svt/SVTFileFormat.hpp"
#include <string>
#include <vector>
#include <imgui.h>

namespace windows
{
    // Preview window for .vfSVT (Sparse Virtual Texture) files.
    // Shows file info, tile grid visualization, and individual tile preview.
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
        int highlightTileX = -1;
        int highlightTileY = -1;
        float gridZoom = 1.0f;

        // Tile preview (decoded for display)
        ImTextureID tilePreviewDescriptor = 0;
        int previewTileX = -1;
        int previewTileY = -1;
        int previewTileMip = -1;

    public:
        explicit SVTPreviewWindow(const std::string& path);
        ~SVTPreviewWindow() override;

        void draw() override;
        bool shouldClose() const override { return !isOpen; }

    private:
        void loadFile();
        void drawInfoPanel();
        void drawTileGridPanel();
        void drawTilePreviewPanel();
        void countPresentTiles();
    };
}
