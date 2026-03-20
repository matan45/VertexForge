#include "SVTPreviewWindow.hpp"
#include "imgui.h"
#include "events/EventDispatcher.hpp"
#include "events/render/RenderEvents.hpp"
#include "resource/Types.hpp"
#include "resource/BC7Decoder.hpp"
#include <filesystem>
#include <cmath>
#include <cstring>
#include <algorithm>

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
        // Handle is released in draw() when isOpen becomes false,
        // before ImGui records draw commands for this frame.
        reader.close();
    }

    void SVTPreviewWindow::loadFile()
    {
        loaded = reader.open(filePath);
        if (loaded)
        {
            totalTiles = reader.getTotalTiles();
            countPresentTiles();

            std::error_code ec;
            fileSizeBytes = std::filesystem::file_size(filePath, ec);
            if (ec) fileSizeBytes = 0;

            // Auto-select coarsest mip that has a reasonable number of tiles
            const auto& header = reader.getHeader();
            uint32_t mipLevels = render::svt::computeMipLevelCount(
                header.virtualSizeLog2, header.tileSizeLog2);
            selectedMipLevel = static_cast<int>(mipLevels > 2 ? mipLevels - 3 : 0);

            loadMipPreview(selectedMipLevel);
        }
    }

    void SVTPreviewWindow::countPresentTiles()
    {
        // Count using directory entries (in-memory) instead of reading tile data from disk
        presentTiles = reader.countPresentEntries();
    }

    void SVTPreviewWindow::loadMipPreview(int mipLevel)
    {
        if (!loaded || mipLevel == loadedPreviewMip) return;

        const auto& header = reader.getHeader();
        uint32_t tileSize = 1u << header.tileSizeLog2;
        uint32_t tilesPerSide = render::svt::computeTilesPerMipSide(
            static_cast<uint32_t>(mipLevel), header.virtualSizeLog2, header.tileSizeLog2);
        if (tilesPerSide == 0) tilesPerSide = 1;

        uint32_t fullWidth = tilesPerSide * tileSize;
        uint32_t fullHeight = tilesPerSide * tileSize;

        uint32_t downsample = 1;
        while ((fullWidth / downsample) > 2048 || (fullHeight / downsample) > 2048)
            downsample *= 2;

        assembledWidth = fullWidth / downsample;
        assembledHeight = fullHeight / downsample;

        decodeTilesForMip(mipLevel, downsample);
        uploadPreviewTexture();

        loadedPreviewMip = mipLevel;
    }

    void SVTPreviewWindow::decodeTilesForMip(int mipLevel, uint32_t downsample)
    {
        const auto& header = reader.getHeader();
        uint32_t tileSize = 1u << header.tileSizeLog2;
        uint32_t border = header.borderSize;
        uint32_t physTileSize = tileSize + 2 * border;
        uint32_t tilesPerSide = render::svt::computeTilesPerMipSide(
            static_cast<uint32_t>(mipLevel), header.virtualSizeLog2, header.tileSizeLog2);
        if (tilesPerSide == 0) tilesPerSide = 1;

        assembledMipRGBA.resize(static_cast<size_t>(assembledWidth) * assembledHeight * 4);

        // Fill with checkerboard pattern (for missing tiles)
        for (uint32_t y = 0; y < assembledHeight; ++y)
        {
            for (uint32_t x = 0; x < assembledWidth; ++x)
            {
                size_t idx = (static_cast<size_t>(y) * assembledWidth + x) * 4;
                bool dark = ((x / 16) + (y / 16)) % 2 == 0;
                uint8_t v = dark ? 40 : 60;
                assembledMipRGBA[idx + 0] = v;
                assembledMipRGBA[idx + 1] = v;
                assembledMipRGBA[idx + 2] = v;
                assembledMipRGBA[idx + 3] = 255;
            }
        }

        uint32_t dsTileSize = tileSize / downsample;

        for (uint32_t ty = 0; ty < tilesPerSide; ++ty)
        {
            for (uint32_t tx = 0; tx < tilesPerSide; ++tx)
            {
                std::vector<uint8_t> tileData;
                render::svt::VirtualTileCoord coord{tx, ty, static_cast<uint32_t>(mipLevel)};
                if (!reader.readTile(coord, tileData) || tileData.empty()) continue;

                auto decoded = resource::BC7Decoder::decompress(
                    tileData.data(), physTileSize, physTileSize);
                if (decoded.empty()) continue;

                for (uint32_t py = 0; py < dsTileSize; ++py)
                {
                    for (uint32_t px = 0; px < dsTileSize; ++px)
                    {
                        uint32_t imgX = tx * dsTileSize + px;
                        uint32_t imgY = ty * dsTileSize + py;
                        if (imgX >= assembledWidth || imgY >= assembledHeight) continue;

                        uint32_t srcX = px * downsample + border;
                        uint32_t srcY = py * downsample + border;
                        size_t srcIdx = (static_cast<size_t>(srcY) * physTileSize + srcX) * 4;
                        size_t dstIdx = (static_cast<size_t>(imgY) * assembledWidth + imgX) * 4;

                        if (srcIdx + 3 < decoded.size())
                        {
                            assembledMipRGBA[dstIdx + 0] = decoded[srcIdx + 0];
                            assembledMipRGBA[dstIdx + 1] = decoded[srcIdx + 1];
                            assembledMipRGBA[dstIdx + 2] = decoded[srcIdx + 2];
                            assembledMipRGBA[dstIdx + 3] = decoded[srcIdx + 3];
                        }
                    }
                }
            }
        }
    }

    void SVTPreviewWindow::uploadPreviewTexture()
    {
        if (mipPreviewHandle.isValid())
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::render::ReleaseEditorTextureCommand releaseCmd;
            releaseCmd.handle = mipPreviewHandle.imguiDescriptorSet;
            dispatcher.execute(releaseCmd);
            mipPreviewHandle = {};
        }

        if (assembledWidth == 0 || assembledHeight == 0) return;

        auto& dispatcher = events::EventDispatcher::instance();

        events::render::LoadEditorTextureFromDataCommand loadCmd;
        loadCmd.textureData.width = assembledWidth;
        loadCmd.textureData.height = assembledHeight;
        loadCmd.textureData.numbersOfChannels = 4;
        loadCmd.textureData.mipLevels = 1;
        loadCmd.textureData.compressionFormat = resource::TextureCompressionFormat::Uncompressed;

        resource::MipLevelData mip0;
        mip0.width = assembledWidth;
        mip0.height = assembledHeight;
        mip0.dataSize = static_cast<uint32_t>(assembledMipRGBA.size());
        mip0.data = assembledMipRGBA;
        loadCmd.textureData.mipData.push_back(std::move(mip0));

        mipPreviewHandle = dispatcher.execute(loadCmd);
        mipPreviewLoading = false;
    }

    void SVTPreviewWindow::draw()
    {
        if (!isOpen)
        {
            // Release texture handle before destruction so ImGui doesn't reference a freed descriptor
            if (mipPreviewHandle.isValid())
            {
                auto& dispatcher = events::EventDispatcher::instance();
                events::render::ReleaseEditorTextureCommand releaseCmd;
                releaseCmd.handle = mipPreviewHandle.imguiDescriptorSet;
                dispatcher.execute(releaseCmd);
                mipPreviewHandle = {};
            }
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

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

            // Right: texture preview
            float previewWidth = contentSize.x - panelWidth - ImGui::GetStyle().ItemSpacing.x;
            ImGui::BeginChild("SVTPreview", ImVec2(previewWidth, contentSize.y), true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            drawPreviewPanel();
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

        int prevMip = selectedMipLevel;
        ImGui::SliderInt("##Mip", &selectedMipLevel, 0, static_cast<int>(mipLevels - 1));
        if (selectedMipLevel != prevMip)
        {
            loadMipPreview(selectedMipLevel);
        }

        ImGui::Text("Tiles: %dx%d", tilesAtMip, tilesAtMip);
        uint32_t mipRes = virtualSize >> selectedMipLevel;
        ImGui::Text("Resolution: %dx%d", mipRes, mipRes);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Zoom");
        ImGui::SliderFloat("##GridZoom", &gridZoom, 0.25f, 5.0f, "%.2fx");

        if (ImGui::Button("Reset View", ImVec2(-1, 0)))
        {
            gridZoom = 1.0f;
        }
    }

    void SVTPreviewWindow::drawPreviewPanel()
    {
        if (mipPreviewHandle.isValid())
        {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float imageAspect = static_cast<float>(assembledWidth) / std::max(1.0f, static_cast<float>(assembledHeight));
            float availAspect = avail.x / std::max(1.0f, avail.y);

            ImVec2 imageSize;
            if (imageAspect > availAspect)
            {
                imageSize.x = avail.x * gridZoom;
                imageSize.y = imageSize.x / imageAspect;
            }
            else
            {
                imageSize.y = avail.y * gridZoom;
                imageSize.x = imageSize.y * imageAspect;
            }

            ImGui::Image(mipPreviewHandle.imguiDescriptorSet, imageSize);

            // Tooltip on hover
            if (ImGui::IsItemHovered())
            {
                ImVec2 itemPos = ImGui::GetItemRectMin();
                ImVec2 mousePos = ImGui::GetMousePos();
                float relX = (mousePos.x - itemPos.x) / imageSize.x;
                float relY = (mousePos.y - itemPos.y) / imageSize.y;

                const auto& header = reader.getHeader();
                uint32_t tilesPerSide = render::svt::computeTilesPerMipSide(
                    static_cast<uint32_t>(selectedMipLevel),
                    header.virtualSizeLog2, header.tileSizeLog2);
                if (tilesPerSide == 0) tilesPerSide = 1;

                int tileX = static_cast<int>(relX * tilesPerSide);
                int tileY = static_cast<int>(relY * tilesPerSide);
                tileX = std::clamp(tileX, 0, static_cast<int>(tilesPerSide) - 1);
                tileY = std::clamp(tileY, 0, static_cast<int>(tilesPerSide) - 1);

                std::vector<uint8_t> tmp;
                render::svt::VirtualTileCoord coord{
                    static_cast<uint32_t>(tileX),
                    static_cast<uint32_t>(tileY),
                    static_cast<uint32_t>(selectedMipLevel)};
                bool present = reader.readTile(coord, tmp);

                ImGui::BeginTooltip();
                ImGui::Text("Tile (%d, %d) Mip %d", tileX, tileY, selectedMipLevel);
                ImGui::Text("Status: %s", present ? "Present" : "Missing");
                if (present)
                    ImGui::Text("Data: %zu bytes", tmp.size());
                ImGui::EndTooltip();
            }
        }
        else if (assembledWidth == 0)
        {
            ImGui::TextWrapped("Mip level too large for preview (>4096 px). "
                               "Select a higher mip level to preview.");
        }
        else
        {
            ImGui::TextDisabled("No preview available");
        }
    }
}
