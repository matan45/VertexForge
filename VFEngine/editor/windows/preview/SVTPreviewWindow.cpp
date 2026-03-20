#include "SVTPreviewWindow.hpp"
#include "imgui.h"
#include <filesystem>
#include <cmath>

namespace windows
{
    SVTPreviewWindow::SVTPreviewWindow(const std::string& path)
        : filePath(path)
    {
        std::filesystem::path p(path);
        windowTitle = "SVT Preview: " + p.filename().string();
        loadFile();
    }

    SVTPreviewWindow::~SVTPreviewWindow()
    {
        reader.close();
    }

    void SVTPreviewWindow::loadFile()
    {
        loaded = reader.open(filePath);
        if (loaded)
        {
            totalTiles = reader.getTotalTiles();
            countPresentTiles();

            // Get file size
            std::error_code ec;
            fileSizeBytes = std::filesystem::file_size(filePath, ec);
            if (ec) fileSizeBytes = 0;
        }
    }

    void SVTPreviewWindow::countPresentTiles()
    {
        presentTiles = 0;
        const auto& header = reader.getHeader();
        uint32_t mipLevels = render::svt::computeMipLevelCount(
            header.virtualSizeLog2, header.tileSizeLog2);

        for (uint32_t m = 0; m < mipLevels; ++m)
        {
            uint32_t tps = render::svt::computeTilesPerMipSide(m,
                header.virtualSizeLog2, header.tileSizeLog2);
            if (tps == 0) tps = 1;

            for (uint32_t y = 0; y < tps; ++y)
            {
                for (uint32_t x = 0; x < tps; ++x)
                {
                    std::vector<uint8_t> tmp;
                    if (reader.readTile({x, y, m}, tmp))
                        ++presentTiles;
                }
            }
        }
    }

    void SVTPreviewWindow::draw()
    {
        if (!isOpen) return;

        ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse))
        {
            if (!loaded)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Failed to load SVT file");
                ImGui::End();
                return;
            }

            float panelWidth = 200.0f;
            ImVec2 contentSize = ImGui::GetContentRegionAvail();

            // Left: info panel
            ImGui::BeginChild("SVTInfo", ImVec2(panelWidth, contentSize.y), true);
            drawInfoPanel();
            ImGui::EndChild();

            ImGui::SameLine();

            // Right: tile grid
            float gridWidth = contentSize.x - panelWidth - ImGui::GetStyle().ItemSpacing.x;
            ImGui::BeginChild("SVTGrid", ImVec2(gridWidth, contentSize.y), true);
            drawTileGridPanel();
            ImGui::EndChild();
        }
        ImGui::End();
    }

    void SVTPreviewWindow::drawInfoPanel()
    {
        const auto& header = reader.getHeader();
        uint32_t virtualSize = 1u << header.virtualSizeLog2;
        uint32_t tileSize = 1u << header.tileSizeLog2;
        uint32_t mipLevels = render::svt::computeMipLevelCount(
            header.virtualSizeLog2, header.tileSizeLog2);

        ImGui::Text("SVT File Info");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Virtual Size: %dx%d", virtualSize, virtualSize);
        ImGui::Text("Tile Size: %dx%d", tileSize, tileSize);
        ImGui::Text("Border: %d px", header.borderSize);
        ImGui::Text("Mip Levels: %d", mipLevels);
        ImGui::Text("Compression: %s", header.compressionFormat == 1 ? "BC7" : "None");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Tile Stats");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Total Tiles: %d", totalTiles);
        ImGui::Text("Present: %d", presentTiles);
        float occupancy = totalTiles > 0 ? 100.0f * presentTiles / totalTiles : 0.0f;
        ImGui::Text("Occupancy: %.1f%%", occupancy);

        if (fileSizeBytes > 0)
        {
            if (fileSizeBytes > 1024 * 1024)
                ImGui::Text("File Size: %.1f MB", fileSizeBytes / (1024.0f * 1024.0f));
            else
                ImGui::Text("File Size: %.1f KB", fileSizeBytes / 1024.0f);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Mip Level");
        ImGui::Separator();
        ImGui::Spacing();

        uint32_t tilesAtMip = render::svt::computeTilesPerMipSide(
            static_cast<uint32_t>(selectedMipLevel), header.virtualSizeLog2, header.tileSizeLog2);
        if (tilesAtMip == 0) tilesAtMip = 1;

        ImGui::SliderInt("##Mip", &selectedMipLevel, 0, static_cast<int>(mipLevels - 1));
        ImGui::Text("Tiles: %dx%d", tilesAtMip, tilesAtMip);

        uint32_t mipRes = virtualSize >> selectedMipLevel;
        ImGui::Text("Resolution: %dx%d", mipRes, mipRes);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Zoom");
        ImGui::SliderFloat("##GridZoom", &gridZoom, 0.5f, 5.0f, "%.1fx");
    }

    void SVTPreviewWindow::drawTileGridPanel()
    {
        const auto& header = reader.getHeader();
        uint32_t tilesPerSide = render::svt::computeTilesPerMipSide(
            static_cast<uint32_t>(selectedMipLevel),
            header.virtualSizeLog2, header.tileSizeLog2);
        if (tilesPerSide == 0) tilesPerSide = 1;

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float cellSize = std::min(avail.x, avail.y) / static_cast<float>(tilesPerSide) * gridZoom;
        cellSize = std::max(cellSize, 2.0f);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();

        // Draw tile grid
        for (uint32_t y = 0; y < tilesPerSide; ++y)
        {
            for (uint32_t x = 0; x < tilesPerSide; ++x)
            {
                ImVec2 p0(origin.x + x * cellSize, origin.y + y * cellSize);
                ImVec2 p1(p0.x + cellSize - 1.0f, p0.y + cellSize - 1.0f);

                // Check if tile is present
                std::vector<uint8_t> tmp;
                render::svt::VirtualTileCoord coord{x, y, static_cast<uint32_t>(selectedMipLevel)};
                bool present = reader.readTile(coord, tmp);

                ImU32 color;
                if (present)
                    color = IM_COL32(50, 180, 50, 200);   // Green = present
                else
                    color = IM_COL32(60, 60, 60, 200);     // Dark gray = missing

                // Highlight hovered tile
                if (static_cast<int>(x) == highlightTileX && static_cast<int>(y) == highlightTileY)
                    color = IM_COL32(255, 255, 100, 220);  // Yellow = hovered

                drawList->AddRectFilled(p0, p1, color);
                drawList->AddRect(p0, p1, IM_COL32(30, 30, 30, 255));
            }
        }

        // Handle mouse hover
        ImVec2 mousePos = ImGui::GetMousePos();
        if (mousePos.x >= origin.x && mousePos.y >= origin.y)
        {
            int mx = static_cast<int>((mousePos.x - origin.x) / cellSize);
            int my = static_cast<int>((mousePos.y - origin.y) / cellSize);
            if (mx >= 0 && mx < static_cast<int>(tilesPerSide) &&
                my >= 0 && my < static_cast<int>(tilesPerSide))
            {
                highlightTileX = mx;
                highlightTileY = my;

                // Tooltip
                std::vector<uint8_t> tmp;
                render::svt::VirtualTileCoord coord{
                    static_cast<uint32_t>(mx),
                    static_cast<uint32_t>(my),
                    static_cast<uint32_t>(selectedMipLevel)};
                bool present = reader.readTile(coord, tmp);

                ImGui::BeginTooltip();
                ImGui::Text("Tile (%d, %d) Mip %d", mx, my, selectedMipLevel);
                ImGui::Text("Status: %s", present ? "Present" : "Missing");
                if (present)
                    ImGui::Text("Data: %zu bytes", tmp.size());
                ImGui::EndTooltip();
            }
            else
            {
                highlightTileX = highlightTileY = -1;
            }
        }

        // Set dummy to make the child scrollable
        float totalSize = tilesPerSide * cellSize;
        ImGui::Dummy(ImVec2(totalSize, totalSize));

        // Legend
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.2f, 0.7f, 0.2f, 1.0f), "Green = Present");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.0f), "Gray = Missing");
    }
}
