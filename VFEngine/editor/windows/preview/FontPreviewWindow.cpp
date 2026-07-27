#include "FontPreviewWindow.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/FontAtlasPreview.hpp"
#include "asset/AssetRef.hpp"
#include "events/EventDispatcher.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/project/ResourceEvents.hpp"
#include "math/MathHelper.hpp"
#include "text/TextLayout.hpp"
#include "text/FontStyleFace.hpp"
#include <imgui.h>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <stdexcept>

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

        // Pick up a reimport of the font currently on screen (VK-1629). Compared
        // against the same path this window was opened with, normalised, because the
        // importer reports its output path as it built it.
        importCompletedToken = events::EventDispatcher::instance()
            .subscribe<events::resource::ImportCompletedNotification>(
                [this](const events::resource::ImportCompletedNotification& notification)
                {
                    std::error_code ec;
                    const auto mine = std::filesystem::weakly_canonical(fontPath, ec);
                    if (ec) return;

                    for (const auto& result : notification.results)
                    {
                        if (!result.success || result.outputPath.empty()) continue;

                        std::error_code compareEc;
                        const auto theirs =
                            std::filesystem::weakly_canonical(result.outputPath, compareEc);
                        if (!compareEc && theirs == mine)
                        {
                            reloadRequested.store(true);
                            return;
                        }
                    }
                });
    }

    FontPreviewWindow::~FontPreviewWindow()
    {
        if (importCompletedToken.isValid())
            events::EventDispatcher::instance().unsubscribe(importCompletedToken);

        loadingCancelled.store(true);

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
            catch (...) {}
        }

        if (atlasHandle.isValid())
        {
            try
            {
                events::render::ReleaseEditorTextureCommand releaseCmd;
                releaseCmd.handle = atlasHandle.imguiDescriptorSet;
                events::EventDispatcher::instance().execute(releaseCmd);
            }
            catch (...) {}
        }
    }

    void FontPreviewWindow::draw()
    {
        if (!isOpen) return;

        if (needsInit) { startAsyncLoad(); needsInit = false; }
        // Deliberately after the needsInit branch and before updateAsyncLoading, so a
        // reload never races the initial load in flight.
        if (reloadRequested.exchange(false) && !loadingInProgress.load()) { reloadFromDisk(); }
        updateAsyncLoading();

        if (initialSize.x <= 0.0f)
        {
            initialSize = editor::preview::initialWindowSize("FontPreview", ImVec2(900, 600));
        }
        ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
        maximizer.preBegin();

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse | maximizer.windowFlags()))
        {
            if (isOpen)
            {
                maximizer.drawButton();

                static float panelWidth = 220.0f;
                const float splitterThickness = 5.0f;
                ImVec2 contentSize = ImGui::GetContentRegionAvail();
                panelWidth = std::clamp(panelWidth, 160.0f,
                                        std::max(160.0f, contentSize.x - 300.0f - splitterThickness));
                float previewWidth = contentSize.x - panelWidth - splitterThickness;

                ImGui::BeginChild("InfoPanel", ImVec2(panelWidth, contentSize.y), true);
                drawInfoPanel();
                ImGui::EndChild();

                ImGui::SameLine(0.0f, 0.0f);
                editor::preview::splitterV("##fontSplit", splitterThickness, &panelWidth,
                                           &previewWidth, 160.0f, 300.0f, contentSize.y);
                ImGui::SameLine(0.0f, 0.0f);

                ImGui::BeginChild("PreviewPanel", ImVec2(previewWidth, contentSize.y), true);
                loadingInProgress.load() ? drawLoadingIndicator() : drawPreviewPanel();
                ImGui::EndChild();
            }
        }
        ImGui::End();

        if (!isOpen && !sizeSaved)
        {
            editor::preview::rememberWindowSize("FontPreview", maximizer.effectiveSize());
            sizeSaved = true;
        }
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

    void FontPreviewWindow::reloadFromDisk()
    {
        // ResourceManager caches FontData by GUID and a reimport keeps the GUID, so
        // without this the reload would hand back the pre-reimport atlas.
        resource::ResourceManager::invalidateFontCache(asset::AssetRef::fromPath(fontPath));

        if (atlasHandle.isValid())
        {
            events::render::ReleaseEditorTextureCommand releaseCmd;
            releaseCmd.handle = atlasHandle.imguiDescriptorSet;
            events::EventDispatcher::instance().execute(releaseCmd);
            atlasHandle = services::EditorTextureHandle{};
        }

        fontData = resource::FontData{};
        fontLoaded = false;
        loadFailed = false;
        errorMessage.clear();

        startAsyncLoad();
    }

    void FontPreviewWindow::updateAsyncLoading()
    {
        if (!loadingInProgress.load() || !loadFuture.valid()) return;
        if (loadFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;

        try {
            FontLoadResult result = loadFuture.get();
            if (result.success) {
                events::render::LoadEditorTextureFromDataCommand loadCmd;
                loadCmd.textureData = std::move(result.atlasAsRGBA);
                atlasHandle = events::EventDispatcher::instance().execute(loadCmd);
                fontData = std::move(result.fontData);
                fontLoaded = true;
            } else {
                loadFailed = true;
                errorMessage = result.errorMessage;
            }
        }
        catch (const std::exception& e) { loadFailed = true; errorMessage = std::string("Exception: ") + e.what(); }
        catch (...) { loadFailed = true; errorMessage = "Unknown exception during font loading"; }

        loadingInProgress.store(false);
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

            auto fontFuture = resource::ResourceManager::loadFontAsync(asset::AssetRef::fromPath(path));
            auto fontPtr = fontFuture.get();
            if (!fontPtr)
            {
                result.errorMessage = "Failed to load font data";
                return result;
            }
            result.fontData = *fontPtr;

            if (result.fontData.glyphs.empty())
            {
                result.errorMessage = "Failed to load font data";
                return result;
            }

            if (loadingCancelled.load())
            {
                result.fontData = resource::FontData{};
                result.errorMessage = "Cancelled";
                return result;
            }

            result.atlasAsRGBA = resource::fontAtlasToPreviewRGBA(result.fontData);

            result.success = true;
        }
        catch (const std::bad_alloc& e)
        {
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

    // VK-1636. Bold and italic are synthesized unless the family ships a real face
    // next to this one, and nothing in the editor used to say which you were looking
    // at - a "bold" label that quietly stayed a thickened Regular looked like a bug in
    // the renderer. This panel answers it with the same naming convention the renderer
    // resolves through, so the two can never disagree.
    void FontPreviewWindow::drawStyleFamilyPanel()
    {
        if (!ImGui::CollapsingHeader("Style Family", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        struct StyleRow
        {
            const char* label;
            uint32_t bits;
        };
        static constexpr StyleRow rows[] = {
            {"Bold", ::text::STYLE_BOLD},
            {"Italic", ::text::STYLE_ITALIC},
            {"Bold Italic", ::text::STYLE_BOLD | ::text::STYLE_ITALIC},
        };

        for (const StyleRow& row : rows)
        {
            std::string found;
            for (const std::string& candidate : ::text::styledPathCandidates(fontPath, row.bits))
            {
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec))
                {
                    found = candidate;
                    break;
                }
            }

            if (found.empty())
            {
                ImGui::TextDisabled("%s: synthesized", row.label);
                if (ImGui::IsItemHovered())
                {
                    // Name the file the renderer will pick up, so importing the real
                    // face is a matter of matching this name.
                    const auto candidates = ::text::styledPathCandidates(fontPath, row.bits);
                    if (!candidates.empty())
                    {
                        ImGui::SetTooltip("No sibling face. Import one named:\n%s",
                                          std::filesystem::path(candidates.front())
                                              .filename().string().c_str());
                    }
                }
            }
            else
            {
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%s: %s", row.label,
                                   std::filesystem::path(found).filename().string().c_str());
            }
        }
    }

    void FontPreviewWindow::drawInfoPanel()
    {
        ImGui::Text("Font Info");
        ImGui::Separator();

        if (loadFailed) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Failed to load");
            ImGui::TextWrapped("%s", errorMessage.c_str());
            return;
        }
        if (!fontLoaded) { ImGui::TextDisabled("Loading..."); return; }

        if (ImGui::CollapsingHeader("Metadata", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Name:"); ImGui::TextWrapped("  %s", fontData.metadata.fontName.c_str());
            ImGui::Spacing();
            ImGui::Text("Style: %s", fontData.metadata.fontStyle.c_str());
            ImGui::Text("Base Size: %u px", fontData.metadata.baseFontSize);
            if (resource::hasFlag(fontData.formatFlags, resource::FontFormatFlags::COLOR_EMOJI))
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Color Emoji Font");
        }
        ImGui::Spacing();

        drawStyleFamilyPanel();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Metrics", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Line Height: %.2f", fontData.metadata.lineHeight);
            ImGui::Text("Ascender: %.2f", fontData.metadata.ascender);
            ImGui::Text("Descender: %.2f", fontData.metadata.descender);
            ImGui::Text("Underline Pos: %.2f", fontData.metadata.underlinePosition);
            ImGui::Text("Underline Thick: %.2f", fontData.metadata.underlineThickness);
        }
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Atlas", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Size: %dx%d", fontData.atlas.width, fontData.atlas.height);
            const char* formatStr = "Unknown";
            switch (fontData.atlas.format) {
            case resource::FontAtlasFormat::GRAYSCALE_8: formatStr = "Grayscale"; break;
            case resource::FontAtlasFormat::SDF_8: formatStr = "SDF"; break;
            case resource::FontAtlasFormat::RGBA_32: formatStr = "RGBA"; break;
            case resource::FontAtlasFormat::MTSDF_RGBA_32: formatStr = "MTSDF"; break;
            }
            ImGui::Text("Format: %s", formatStr);
            ImGui::Text("Glyphs: %zu", fontData.glyphs.size());
            ImGui::Text("Kerning Pairs: %zu", fontData.kerningPairs.size());
        }
        ImGui::Spacing();

        if (fontData.isSDF() && ImGui::CollapsingHeader("SDF Parameters")) {
            ImGui::Text("Spread: %.2f", fontData.sdfParams.spread);
            ImGui::Text("Padding: %u", fontData.sdfParams.padding);
            ImGui::Text("Edge Value: %.2f", fontData.sdfParams.edgeValue);
            if (fontData.isMSDF())
                ImGui::Text("Px Range: %.2f", fontData.sdfParams.pxRange);
        }
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Character Ranges")) {
            for (const auto& range : fontData.characterRanges)
                ImGui::Text("  U+%04X - U+%04X", range.rangeStart, range.rangeEnd);
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
        if (!atlasHandle.isValid()) { ImGui::TextDisabled("Atlas not loaded"); return; }

        ImGui::Text("Character Grid:");
        ImVec2 startPos = ImGui::GetCursorScreenPos();
        ImVec2 availSize = ImGui::GetContentRegionAvail();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(startPos, ImVec2(startPos.x + availSize.x, startPos.y + availSize.y), IM_COL32(30, 30, 30, 255));

        float scale = previewFontSize / static_cast<float>(fontData.metadata.baseFontSize);
        float cellSize = previewFontSize + 8.0f;
        int glyphsPerRow = std::max(1, static_cast<int>(availSize.x / cellSize));
        float x = startPos.x + 4.0f, y = startPos.y + 4.0f;
        int col = 0;

        for (const auto& glyph : fontData.glyphs) {
            if (y + cellSize > startPos.y + availSize.y) break;

            float u0 = static_cast<float>(glyph.atlasX) / fontData.atlas.width;
            float v0 = static_cast<float>(glyph.atlasY) / fontData.atlas.height;
            float u1 = static_cast<float>(glyph.atlasX + glyph.atlasWidth) / fontData.atlas.width;
            float v1 = static_cast<float>(glyph.atlasY + glyph.atlasHeight) / fontData.atlas.height;
            float glyphW = glyph.atlasWidth * scale, glyphH = glyph.atlasHeight * scale;
            float offsetX = (cellSize - glyphW) * 0.5f, offsetY = (cellSize - glyphH) * 0.5f;

            drawList->AddImage(atlasHandle.imguiDescriptorSet,
                ImVec2(x + offsetX, y + offsetY), ImVec2(x + offsetX + glyphW, y + offsetY + glyphH),
                ImVec2(u0, v0), ImVec2(u1, v1), IM_COL32(255, 255, 255, 255));

            if (++col >= glyphsPerRow) { col = 0; x = startPos.x + 4.0f; y += cellSize; }
            else { x += cellSize; }
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
        drawList->AddRectFilled(windowPos, ImVec2(windowPos.x + availSize.x, windowPos.y + availSize.y), IM_COL32(30, 30, 30, 255));

        float time = static_cast<float>(ImGui::GetTime());
        ImVec2 center(windowPos.x + availSize.x * 0.5f, windowPos.y + availSize.y * 0.5f - 20.0f);
        float startAngle = time * 4.0f, arcLength = 3.14159f * 1.3f;

        for (int i = 0; i < 12; ++i) {
            float t1 = static_cast<float>(i) / 12.0f, t2 = static_cast<float>(i + 1) / 12.0f;
            float a1 = startAngle + t1 * arcLength, a2 = startAngle + t2 * arcLength;
            ImU32 col = IM_COL32(100, 180, 255, static_cast<int>(255 * (1.0f - t1 * 0.7f)));
            drawList->AddLine(ImVec2(center.x + cosf(a1) * 20.0f, center.y + sinf(a1) * 20.0f),
                              ImVec2(center.x + cosf(a2) * 20.0f, center.y + sinf(a2) * 20.0f), col, 4.0f);
        }

        const char* loadingText = "Loading font...";
        ImVec2 textSize = ImGui::CalcTextSize(loadingText);
        ImGui::SetCursorPos(ImVec2((availSize.x - textSize.x) * 0.5f, availSize.y * 0.5f + 20.0f));
        ImGui::Text("%s", loadingText);
        ImGui::Dummy(availSize);
    }

    void FontPreviewWindow::renderTextWithGlyphs(const std::string& text, float fontSize, ImVec2 startPos)
    {
        if (!atlasHandle.isValid() || text.empty())
        {
            return;
        }

        auto layout = text::layoutText(fontData, text, fontSize);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        for (const auto& glyph : layout.glyphs)
        {
            float x = startPos.x + glyph.offset.x;
            float y = startPos.y + glyph.offset.y;

            // VK-1638: a tofu carries no atlas cell - its uvRect is the quad-local
            // (0,0,1,1) the shader reinterprets, so feeding it to AddImage would blit
            // the WHOLE atlas into the glyph box. Stroke the same hollow box the
            // shader draws instead, so the preview reports missing coverage the way
            // the engine renders it.
            if (glyph.faceIndex == text::TOFU_FACE_INDEX)
            {
                const float thickness = std::max(1.0f, glyph.size.y * 0.08f);
                drawList->AddRect(
                    ImVec2(x, y),
                    ImVec2(x + glyph.size.x, y + glyph.size.y),
                    IM_COL32(255, 255, 255, 255),
                    0.0f,
                    ImDrawFlags_None,
                    thickness
                );
                continue;
            }

            drawList->AddImage(
                atlasHandle.imguiDescriptorSet,
                ImVec2(x, y),
                ImVec2(x + glyph.size.x, y + glyph.size.y),
                ImVec2(glyph.uvRect.x, glyph.uvRect.y),
                ImVec2(glyph.uvRect.z, glyph.uvRect.w),
                IM_COL32(255, 255, 255, 255)
            );
        }
    }
}
