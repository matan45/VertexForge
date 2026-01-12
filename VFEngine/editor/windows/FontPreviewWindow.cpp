#include "FontPreviewWindow.hpp"
#include "resource/FontResource.hpp"
#include "events/EventDispatcher.hpp"
#include "events/RenderEvents.hpp"
#include "math/MathHelper.hpp"
#include <imgui.h>
#include <filesystem>

namespace windows
{
    FontPreviewWindow::FontPreviewWindow(const std::string& filePath)
        : fontPath(filePath)
    {
        std::filesystem::path path(filePath);
        windowTitle = "Font Preview: " + path.filename().string();

        const char* defaultText = "The quick brown fox jumps over the lazy dog.\n"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ\n"
            "abcdefghijklmnopqrstuvwxyz\n"
            "0123456789 !@#$%^&*()";
        std::strncpy(textInputBuffer, defaultText, sizeof(textInputBuffer) - 1);
        textInputBuffer[sizeof(textInputBuffer) - 1] = '\0';
    }

    FontPreviewWindow::~FontPreviewWindow()
    {
        loadingCancelled.store(true);

        // Wait for async operation to complete (required before destroying this object)
        if (loadFuture.valid())
        {
            try
            {
                loadFuture.wait();
                if (loadFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                {
                    (void)loadFuture.get();
                }
            }
            catch (...)
            {
                // Suppress exceptions in destructor - nothing we can do here
            }
        }

        if (atlasHandle.isValid())
        {
            try
            {
                events::render::ReleaseEditorTextureCommand releaseCmd;
                releaseCmd.handle = atlasHandle.imguiDescriptorSet;
                events::EventDispatcher::instance().execute(releaseCmd);
            }
            catch (...)
            {
                // Suppress exceptions in destructor - GPU resource may leak
                // but we cannot throw from destructor
            }
        }
    }

    void FontPreviewWindow::draw()
    {
        if (!isOpen)
        {
            return;
        }

        if (needsInit)
        {
            startAsyncLoad();
            needsInit = false;
        }

        updateAsyncLoading();

        ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse))
        {
            if (isOpen)
            {
                float panelWidth = 220.0f;
                ImVec2 contentSize = ImGui::GetContentRegionAvail();

                // Left panel - Info
                ImGui::BeginChild("InfoPanel", ImVec2(panelWidth, contentSize.y), true);
                drawInfoPanel();
                ImGui::EndChild();

                ImGui::SameLine();

                // Right panel - Preview
                float previewWidth = contentSize.x - panelWidth - ImGui::GetStyle().ItemSpacing.x;
                ImGui::BeginChild("PreviewPanel", ImVec2(previewWidth, contentSize.y), true);

                if (loadingInProgress.load())
                {
                    drawLoadingIndicator();
                }
                else
                {
                    drawPreviewPanel();
                }

                ImGui::EndChild();
            }
        }
        ImGui::End();
    }

    void FontPreviewWindow::startAsyncLoad()
    {
        loadingInProgress.store(true);
        loadingCancelled.store(false);

        loadFuture = std::async(std::launch::async, [this]()
        {
            return loadFontBackground(fontPath);
        });
    }

    void FontPreviewWindow::updateAsyncLoading()
    {
        if (!loadingInProgress.load() || !loadFuture.valid())
        {
            return;
        }

        if (loadFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        {
            try
            {
                FontLoadResult result = loadFuture.get();

                if (result.success)
                {
                    // Upload atlas texture to GPU first (may throw)
                    // Do this before modifying member state for exception safety
                    auto& dispatcher = events::EventDispatcher::instance();
                    events::render::LoadEditorTextureFromDataCommand loadCmd;
                    loadCmd.textureData = std::move(result.atlasAsRGBA);
                    services::EditorTextureHandle newHandle = dispatcher.execute(loadCmd);

                    // Only update member state after all operations that may throw succeed
                    // This ensures consistent state if an exception occurs
                    fontData = std::move(result.fontData);
                    atlasHandle = newHandle;
                    fontLoaded = true;
                }
                else
                {
                    loadFailed = true;
                    errorMessage = result.errorMessage;
                }
            }
            catch (const std::exception& e)
            {
                loadFailed = true;
                errorMessage = std::string("Exception: ") + e.what();
            }
            catch (...)
            {
                loadFailed = true;
                errorMessage = "Unknown exception during font loading";
            }

            loadingInProgress.store(false);
        }
    }

    FontLoadResult FontPreviewWindow::loadFontBackground(const std::string& path)
    {
        FontLoadResult result;

        try
        {
            if (loadingCancelled.load())
            {
                result.errorMessage = "Cancelled";
                return result;
            }

            result.fontData = resource::FontResource::loadFont(path);

            if (result.fontData.glyphs.empty())
            {
                result.errorMessage = "Failed to load font data";
                return result;
            }

            if (loadingCancelled.load())
            {
                // Clean up already-loaded font data before returning
                result.fontData = resource::FontData{};
                result.errorMessage = "Cancelled";
                return result;
            }

            // Convert atlas to RGBA for ImGui (may throw std::bad_alloc)
            result.atlasAsRGBA = convertAtlasToRGBA(result.fontData.atlas);

            result.success = true;
        }
        catch (const std::bad_alloc& e)
        {
            // Memory allocation failure - clean up partial state
            result.fontData = resource::FontData{};
            result.atlasAsRGBA = resource::TextureData{};
            result.errorMessage = std::string("Out of memory: ") + e.what();
        }
        catch (const std::exception& e)
        {
            result.errorMessage = std::string("Error: ") + e.what();
        }
        catch (...)
        {
            result.errorMessage = "Unknown error during font loading";
        }

        return result;
    }

    resource::TextureData FontPreviewWindow::convertAtlasToRGBA(const resource::FontAtlasData& atlas)
    {
        resource::TextureData textureData;
        textureData.width = atlas.width;
        textureData.height = atlas.height;
        textureData.numbersOfChannels = 4;
        textureData.mipLevels = 1;

        size_t pixelCount = static_cast<size_t>(atlas.width) * atlas.height;
        std::vector<unsigned char> rgbaData(pixelCount * 4);

        if (atlas.format == resource::FontAtlasFormat::SDF_8)
        {
            // Uses shared utility to ensure consistency with shader logic
            for (size_t i = 0; i < pixelCount; ++i)
            {
                rgbaData[i * 4 + 0] = 255;
                rgbaData[i * 4 + 1] = 255;
                rgbaData[i * 4 + 2] = 255;
                rgbaData[i * 4 + 3] = sdf::sdfToAlphaByte(atlas.pixels[i]);
            }
        }
        else if (atlas.format == resource::FontAtlasFormat::GRAYSCALE_8)
        {
            for (size_t i = 0; i < pixelCount; ++i)
            {
                unsigned char value = atlas.pixels[i];
                rgbaData[i * 4 + 0] = 255;
                rgbaData[i * 4 + 1] = 255;
                rgbaData[i * 4 + 2] = 255;
                rgbaData[i * 4 + 3] = value;
            }
        }
        else if (atlas.format == resource::FontAtlasFormat::RGBA_32)
        {
            rgbaData.assign(atlas.pixels.begin(), atlas.pixels.end());
        }

        resource::MipLevelData mip;
        mip.width = atlas.width;
        mip.height = atlas.height;
        mip.data = std::move(rgbaData);
        textureData.mipData.push_back(std::move(mip));

        return textureData;
    }

    void FontPreviewWindow::drawInfoPanel()
    {
        ImGui::Text("Font Info");
        ImGui::Separator();

        if (loadFailed)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Failed to load");
            ImGui::TextWrapped("%s", errorMessage.c_str());
            return;
        }

        if (!fontLoaded)
        {
            ImGui::TextDisabled("Loading...");
            return;
        }

        if (ImGui::CollapsingHeader("Metadata", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Name:");
            ImGui::TextWrapped("  %s", fontData.metadata.fontName.c_str());
            ImGui::Spacing();

            ImGui::Text("Style: %s", fontData.metadata.fontStyle.c_str());
            ImGui::Text("Base Size: %u px", fontData.metadata.baseFontSize);
        }

        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Metrics", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Line Height: %.2f", fontData.metadata.lineHeight);
            ImGui::Text("Ascender: %.2f", fontData.metadata.ascender);
            ImGui::Text("Descender: %.2f", fontData.metadata.descender);
            ImGui::Text("Underline Pos: %.2f", fontData.metadata.underlinePosition);
            ImGui::Text("Underline Thick: %.2f", fontData.metadata.underlineThickness);
        }

        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Atlas", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Size: %dx%d", fontData.atlas.width, fontData.atlas.height);

            const char* formatStr = "Unknown";
            switch (fontData.atlas.format)
            {
            case resource::FontAtlasFormat::GRAYSCALE_8: formatStr = "Grayscale";
                break;
            case resource::FontAtlasFormat::SDF_8: formatStr = "SDF";
                break;
            case resource::FontAtlasFormat::RGBA_32: formatStr = "RGBA";
                break;
            }
            ImGui::Text("Format: %s", formatStr);
            ImGui::Text("Glyphs: %zu", fontData.glyphs.size());
            ImGui::Text("Kerning Pairs: %zu", fontData.kerningPairs.size());
        }

        ImGui::Spacing();

        if (fontData.isSDF() && ImGui::CollapsingHeader("SDF Parameters"))
        {
            ImGui::Text("Spread: %.2f", fontData.sdfParams.spread);
            ImGui::Text("Padding: %u", fontData.sdfParams.padding);
            ImGui::Text("Edge Value: %.2f", fontData.sdfParams.edgeValue);
        }

        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Character Ranges"))
        {
            for (const auto& range : fontData.characterRanges)
            {
                ImGui::Text("  U+%04X - U+%04X", range.rangeStart, range.rangeEnd);
            }
        }
    }

    void FontPreviewWindow::drawPreviewPanel()
    {
        if (!fontLoaded)
        {
            ImGui::TextDisabled("Font not loaded");
            return;
        }

        ImGui::Text("Preview Settings");
        ImGui::Separator();

        ImGui::SliderFloat("Font Size", &previewFontSize,
                           static_cast<float>(fontData.metadata.baseFontSize) * 0.25f,
                           static_cast<float>(fontData.metadata.baseFontSize) * 4.0f);

        ImGui::Spacing();

        ImGui::Checkbox("Show Character Grid", &showCharacterGrid);
        ImGui::SameLine();
        ImGui::Checkbox("Show Atlas", &showAtlasPreview);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Preview Text:");
        ImGui::InputTextMultiline("##PreviewText", textInputBuffer, sizeof(textInputBuffer),
                                  ImVec2(-1, 80.0f));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (showAtlasPreview)
        {
            drawAtlasPreview();
        }
        else if (showCharacterGrid)
        {
            drawCharacterGrid();
        }
        else
        {
            drawTextPreview();
        }
    }

    void FontPreviewWindow::drawTextPreview()
    {
        if (!atlasHandle.isValid())
        {
            ImGui::TextDisabled("Atlas not loaded");
            return;
        }

        ImGui::Text("Text Preview:");

        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(canvasPos,
                                ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                                IM_COL32(30, 30, 30, 255));

        float baseline = canvasPos.y + fontData.metadata.ascender *
            (previewFontSize / fontData.metadata.baseFontSize) + 10.0f;
        drawList->AddLine(
            ImVec2(canvasPos.x, baseline),
            ImVec2(canvasPos.x + canvasSize.x, baseline),
            IM_COL32(60, 60, 60, 255));

        renderTextWithGlyphs(textInputBuffer, previewFontSize,
                             ImVec2(canvasPos.x + 10.0f, canvasPos.y + 10.0f));

        ImGui::Dummy(canvasSize);
    }

    void FontPreviewWindow::drawCharacterGrid()
    {
        if (!atlasHandle.isValid())
        {
            ImGui::TextDisabled("Atlas not loaded");
            return;
        }

        ImGui::Text("Character Grid:");

        ImVec2 startPos = ImGui::GetCursorScreenPos();
        ImVec2 availSize = ImGui::GetContentRegionAvail();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(startPos,
                                ImVec2(startPos.x + availSize.x, startPos.y + availSize.y),
                                IM_COL32(30, 30, 30, 255));

        float scale = previewFontSize / static_cast<float>(fontData.metadata.baseFontSize);
        float cellSize = previewFontSize + 8.0f;
        int glyphsPerRow = static_cast<int>(availSize.x / cellSize);
        if (glyphsPerRow < 1) glyphsPerRow = 1;

        float x = startPos.x + 4.0f;
        float y = startPos.y + 4.0f;
        int col = 0;

        for (const auto& glyph : fontData.glyphs)
        {
            if (y + cellSize > startPos.y + availSize.y)
            {
                break;
            }

            float u0 = static_cast<float>(glyph.atlasX) / fontData.atlas.width;
            float v0 = static_cast<float>(glyph.atlasY) / fontData.atlas.height;
            float u1 = static_cast<float>(glyph.atlasX + glyph.atlasWidth) / fontData.atlas.width;
            float v1 = static_cast<float>(glyph.atlasY + glyph.atlasHeight) / fontData.atlas.height;

            float glyphW = glyph.atlasWidth * scale;
            float glyphH = glyph.atlasHeight * scale;

            // Center glyph in cell
            float offsetX = (cellSize - glyphW) * 0.5f;
            float offsetY = (cellSize - glyphH) * 0.5f;

            drawList->AddImage(
                atlasHandle.imguiDescriptorSet,
                ImVec2(x + offsetX, y + offsetY),
                ImVec2(x + offsetX + glyphW, y + offsetY + glyphH),
                ImVec2(u0, v0),
                ImVec2(u1, v1),
                IM_COL32(255, 255, 255, 255)
            );

            col++;
            if (col >= glyphsPerRow)
            {
                col = 0;
                x = startPos.x + 4.0f;
                y += cellSize;
            }
            else
            {
                x += cellSize;
            }
        }

        ImGui::Dummy(availSize);
    }

    void FontPreviewWindow::drawAtlasPreview()
    {
        if (!atlasHandle.isValid())
        {
            ImGui::TextDisabled("Atlas not loaded");
            return;
        }

        ImGui::Text("Atlas Preview:");
        ImGui::SliderFloat("Zoom", &atlasZoom, 0.25f, 4.0f);

        ImVec2 availSize = ImGui::GetContentRegionAvail();

        float atlasW = static_cast<float>(fontData.atlas.width) * atlasZoom;
        float atlasH = static_cast<float>(fontData.atlas.height) * atlasZoom;

        // Center if smaller than available space
        ImVec2 imageSize(atlasW, atlasH);
        if (atlasW < availSize.x && atlasH < availSize.y)
        {
            float offsetX = (availSize.x - atlasW) * 0.5f;
            float offsetY = (availSize.y - atlasH) * 0.5f;
            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + offsetX,
                                       ImGui::GetCursorPosY() + offsetY));
        }

        ImGui::Image(atlasHandle.imguiDescriptorSet, imageSize);
    }

    void FontPreviewWindow::drawLoadingIndicator()
    {
        ImVec2 availSize = ImGui::GetContentRegionAvail();
        ImVec2 windowPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        drawList->AddRectFilled(
            windowPos,
            ImVec2(windowPos.x + availSize.x, windowPos.y + availSize.y),
            IM_COL32(30, 30, 30, 255)
        );

        float time = static_cast<float>(ImGui::GetTime());
        float spinnerRadius = 20.0f;
        float spinnerThickness = 4.0f;
        ImVec2 spinnerCenter(
            windowPos.x + availSize.x * 0.5f,
            windowPos.y + availSize.y * 0.5f - 20.0f
        );

        int numSegments = 12;
        float startAngle = time * 4.0f;
        float arcLength = 3.14159f * 1.3f;

        for (int i = 0; i < numSegments; ++i)
        {
            float t1 = static_cast<float>(i) / numSegments;
            float t2 = static_cast<float>(i + 1) / numSegments;
            float angle1 = startAngle + t1 * arcLength;
            float angle2 = startAngle + t2 * arcLength;

            int alpha = static_cast<int>(255 * (1.0f - t1 * 0.7f));
            ImU32 segColor = IM_COL32(100, 180, 255, alpha);

            ImVec2 p1(spinnerCenter.x + cosf(angle1) * spinnerRadius,
                      spinnerCenter.y + sinf(angle1) * spinnerRadius);
            ImVec2 p2(spinnerCenter.x + cosf(angle2) * spinnerRadius,
                      spinnerCenter.y + sinf(angle2) * spinnerRadius);

            drawList->AddLine(p1, p2, segColor, spinnerThickness);
        }

        const char* loadingText = "Loading font...";
        ImVec2 textSize = ImGui::CalcTextSize(loadingText);
        ImGui::SetCursorPos(ImVec2(
            (availSize.x - textSize.x) * 0.5f,
            availSize.y * 0.5f + 20.0f
        ));
        ImGui::Text("%s", loadingText);

        ImGui::Dummy(availSize);
    }

    void FontPreviewWindow::renderTextWithGlyphs(const std::string& text, float fontSize, ImVec2 startPos)
    {
        if (!atlasHandle.isValid() || text.empty())
        {
            return;
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        float scale = fontSize / static_cast<float>(fontData.metadata.baseFontSize);
        float cursorX = startPos.x;
        float cursorY = startPos.y;
        float lineHeight = fontData.metadata.lineHeight * scale;

        uint32_t prevCodepoint = 0;
        size_t i = 0;

        while (i < text.size())
        {
            uint32_t codepoint = decodeUTF8(text, i);

            if (codepoint == '\n')
            {
                cursorX = startPos.x;
                cursorY += lineHeight;
                prevCodepoint = 0;
                continue;
            }

            if (codepoint == '\r')
            {
                continue;
            }

            const resource::GlyphData* glyph = fontData.findGlyph(codepoint);
            if (!glyph)
            {
                // Skip unknown characters
                cursorX += fontSize * 0.5f;
                prevCodepoint = codepoint;
                continue;
            }

            if (prevCodepoint != 0)
            {
                cursorX += fontData.getKerning(prevCodepoint, codepoint) * scale;
            }

            float x = cursorX + glyph->bearingX * scale;
            float y = cursorY + (fontData.metadata.ascender - glyph->bearingY) * scale;
            float w = glyph->atlasWidth * scale;
            float h = glyph->atlasHeight * scale;

            float u0 = static_cast<float>(glyph->atlasX) / fontData.atlas.width;
            float v0 = static_cast<float>(glyph->atlasY) / fontData.atlas.height;
            float u1 = static_cast<float>(glyph->atlasX + glyph->atlasWidth) / fontData.atlas.width;
            float v1 = static_cast<float>(glyph->atlasY + glyph->atlasHeight) / fontData.atlas.height;

            drawList->AddImage(
                atlasHandle.imguiDescriptorSet,
                ImVec2(x, y),
                ImVec2(x + w, y + h),
                ImVec2(u0, v0),
                ImVec2(u1, v1),
                IM_COL32(255, 255, 255, 255)
            );

            cursorX += glyph->advanceX * scale;
            prevCodepoint = codepoint;
        }
    }

    uint32_t FontPreviewWindow::decodeUTF8(const std::string& text, size_t& index)
    {
        if (index >= text.size())
        {
            return 0;
        }

        unsigned char c = static_cast<unsigned char>(text[index]);

        // Helper lambda to validate continuation byte (must be 10xxxxxx pattern)
        auto isValidContinuation = [&text](size_t idx) -> bool
        {
            if (idx >= text.size()) return false;
            unsigned char b = static_cast<unsigned char>(text[idx]);
            return (b & 0xC0) == 0x80;
        };

        // ASCII (0xxxxxxx)
        if ((c & 0x80) == 0)
        {
            index++;
            return c;
        }

        // 2-byte sequence (110xxxxx 10xxxxxx)
        if ((c & 0xE0) == 0xC0)
        {
            if (!isValidContinuation(index + 1))
            {
                index++;
                return 0xFFFD;
            }
            uint32_t codepoint = (c & 0x1F) << 6;
            codepoint |= (static_cast<unsigned char>(text[index + 1]) & 0x3F);
            index += 2;
            // Reject overlong encodings (codepoint must be >= 0x80 for 2-byte)
            if (codepoint < 0x80)
            {
                return 0xFFFD;
            }
            return codepoint;
        }

        // 3-byte sequence (1110xxxx 10xxxxxx 10xxxxxx)
        if ((c & 0xF0) == 0xE0)
        {
            if (!isValidContinuation(index + 1) || !isValidContinuation(index + 2))
            {
                index++;
                return 0xFFFD;
            }
            uint32_t codepoint = (c & 0x0F) << 12;
            codepoint |= (static_cast<unsigned char>(text[index + 1]) & 0x3F) << 6;
            codepoint |= (static_cast<unsigned char>(text[index + 2]) & 0x3F);
            index += 3;
            // Reject overlong encodings (codepoint must be >= 0x800 for 3-byte)
            // Also reject UTF-16 surrogate pairs (0xD800-0xDFFF)
            if (codepoint < 0x800 || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
            {
                return 0xFFFD;
            }
            return codepoint;
        }

        // 4-byte sequence (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
        if ((c & 0xF8) == 0xF0)
        {
            if (!isValidContinuation(index + 1) || !isValidContinuation(index + 2) ||
                !isValidContinuation(index + 3))
            {
                index++;
                return 0xFFFD;
            }
            uint32_t codepoint = (c & 0x07) << 18;
            codepoint |= (static_cast<unsigned char>(text[index + 1]) & 0x3F) << 12;
            codepoint |= (static_cast<unsigned char>(text[index + 2]) & 0x3F) << 6;
            codepoint |= (static_cast<unsigned char>(text[index + 3]) & 0x3F);
            index += 4;
            // Reject overlong encodings (codepoint must be >= 0x10000 for 4-byte)
            // Also reject codepoints beyond Unicode max (0x10FFFF)
            if (codepoint < 0x10000 || codepoint > 0x10FFFF)
            {
                return 0xFFFD;
            }
            return codepoint;
        }

        // Invalid lead byte (10xxxxxx continuation without lead, or 11111xxx invalid)
        index++;
        return 0xFFFD;
    }
}
