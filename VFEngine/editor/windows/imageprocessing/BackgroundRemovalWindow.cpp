#include "BackgroundRemovalWindow.hpp"
#include "events/render/RenderEvents.hpp"
#include "resource/Types.hpp"
#include "resource/TextureResource.hpp"
#include "resource/BC7Decoder.hpp"
#include "asset/AssetMetadataSerializer.hpp"
#include "asset/AssetMetadata.hpp"
#include <imgui.h>
#include <filesystem>
#include <chrono>
#include <ctime>

namespace windows
{
    BackgroundRemovalWindow::~BackgroundRemovalWindow()
    {
        releaseTextures();
    }

    void BackgroundRemovalWindow::show()
    {
        visible = true;
    }

    void BackgroundRemovalWindow::showWithFile(const std::string& filePath)
    {
        visible = true;
        loadInputImage(filePath);
    }

    void BackgroundRemovalWindow::draw()
    {
        if (!visible)
            return;

        // Poll export
        if (exporting)
            pollExport();

        // Process if dirty (with debounce)
        if (processDirty && !sourcePixels.empty())
        {
            float currentTime = static_cast<float>(ImGui::GetTime());
            if (currentTime - lastProcessTime >= processDebounceTime)
            {
                processImage();
                processDirty = false;
                lastProcessTime = currentTime;
            }
        }

        ImGui::SetNextWindowSize(ImVec2(900, 550), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Remove Background", &visible))
        {
            // Left: controls
            ImGui::BeginChild("Controls", ImVec2(280, 0), true);
            drawControls();
            ImGui::Separator();
            drawExportSection();
            ImGui::EndChild();

            ImGui::SameLine();

            // Right: side-by-side preview
            ImGui::BeginChild("Preview", ImVec2(0, 0), true);
            drawPreview();
            ImGui::EndChild();
        }
        ImGui::End();
    }

    void BackgroundRemovalWindow::drawControls()
    {
        ImGui::Text("Input");
        ImGui::Separator();

        if (ImGui::Button("Load .vfImage..."))
        {
            std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
                {L"VF Image (*.vfImage)", L"*.vfImage"}
            };
            std::string path = fileDialog.openFileDialog(fileTypes);
            if (!path.empty())
                loadInputImage(path);
        }
        ImGui::SameLine();
        if (inputPath.empty())
            ImGui::TextDisabled("(no image loaded)");
        else
        {
            std::string filename = std::filesystem::path(inputPath).filename().string();
            ImGui::Text("%s (%ux%u)", filename.c_str(), sourceWidth, sourceHeight);
        }

        if (sourcePixels.empty())
            return;

        ImGui::Spacing();
        ImGui::Text("Detection");
        ImGui::Separator();

        // Method
        const char* methods[] = { "Color", "Luminance", "Edge-Aware" };
        if (ImGui::Combo("Method", &methodIndex, methods, 3))
        {
            params.method = static_cast<imageprocessing::DetectionMethod>(methodIndex);
            processDirty = true;
        }

        // Threshold
        if (ImGui::SliderFloat("Threshold", &params.threshold, 0.0f, 1.0f, "%.2f"))
            processDirty = true;

        // Feather
        if (ImGui::SliderFloat("Feather", &params.feather, 0.0f, 0.2f, "%.3f"))
            processDirty = true;

        // Auto-detect
        if (ImGui::Checkbox("Auto-detect BG color", &params.autoDetectColor))
            processDirty = true;

        // Color picker (disabled when auto-detect is on)
        if (!params.autoDetectColor)
        {
            if (ImGui::ColorEdit3("BG Color", params.bgColor))
                processDirty = true;
        }
        else if (lastResult.valid())
        {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Detected: (%.0f, %.0f, %.0f)",
                lastResult.detectedBgColor[0] * 255.0f,
                lastResult.detectedBgColor[1] * 255.0f,
                lastResult.detectedBgColor[2] * 255.0f);
        }

        // Invert
        if (ImGui::Checkbox("Invert selection", &params.invertSelection))
            processDirty = true;
    }

    void BackgroundRemovalWindow::drawPreview()
    {
        ImVec2 available = ImGui::GetContentRegionAvail();

        if (!originalHandle.isValid() && !resultHandle.isValid())
        {
            ImGui::TextDisabled("Load a .vfImage to begin");
            return;
        }

        ImGui::Text("Original");
        ImGui::SameLine(available.x * 0.5f);
        ImGui::Text("Result (alpha)");
        ImGui::Separator();

        float halfWidth = (available.x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        float previewHeight = available.y - 40.0f;

        // Original preview
        if (originalHandle.isValid())
        {
            ImGui::BeginChild("OrigPreview", ImVec2(halfWidth, previewHeight), false);
            ImVec2 imgAvail = ImGui::GetContentRegionAvail();
            float aspect = static_cast<float>(sourceWidth) / static_cast<float>(sourceHeight);
            float imgW = imgAvail.x;
            float imgH = imgW / aspect;
            if (imgH > imgAvail.y) { imgH = imgAvail.y; imgW = imgH * aspect; }
            ImGui::Image(originalHandle.imguiDescriptorSet, ImVec2(imgW, imgH));
            ImGui::EndChild();
        }

        ImGui::SameLine();

        // Result preview (with checkerboard composited)
        if (resultHandle.isValid())
        {
            ImGui::BeginChild("ResPreview", ImVec2(halfWidth, previewHeight), false);
            ImVec2 imgAvail = ImGui::GetContentRegionAvail();
            float aspect = static_cast<float>(lastResult.width) / static_cast<float>(lastResult.height);
            float imgW = imgAvail.x;
            float imgH = imgW / aspect;
            if (imgH > imgAvail.y) { imgH = imgAvail.y; imgW = imgH * aspect; }
            ImGui::Image(resultHandle.imguiDescriptorSet, ImVec2(imgW, imgH));
            ImGui::EndChild();
        }
        else
        {
            ImGui::BeginChild("ResPreview", ImVec2(halfWidth, previewHeight), false);
            ImGui::TextDisabled("Processing...");
            ImGui::EndChild();
        }
    }

    void BackgroundRemovalWindow::drawExportSection()
    {
        ImGui::Text("Export");
        ImGui::Separator();

        bool hasResult = lastResult.valid();
        ImGui::BeginDisabled(!hasResult);

        if (ImGui::Button("Save as..."))
        {
            std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
                {L"VF Image (*.vfImage)", L"*.vfImage"}
            };
            std::string path = fileDialog.saveFileDialog(fileTypes, L"vfImage");
            if (!path.empty())
                exportPath = path;
        }
        ImGui::SameLine();
        ImGui::TextWrapped("%s", exportPath.empty() ? "(no path)" : exportPath.c_str());

        bool canExport = hasResult && !exportPath.empty() && !exporting;
        ImGui::BeginDisabled(!canExport);
        if (ImGui::Button("Export", ImVec2(-1.0f, 28.0f)))
            startExport();
        ImGui::EndDisabled();

        ImGui::EndDisabled();

        if (exporting)
            ImGui::TextDisabled("Exporting...");

        if (!exportStatusMessage.empty())
            ImGui::TextWrapped("%s", exportStatusMessage.c_str());
    }

    void BackgroundRemovalWindow::loadInputImage(const std::string& path)
    {
        inputPath = path;
        sourcePixels.clear();
        sourceWidth = sourceHeight = 0;

        // Load .vfImage using TextureResource (handles both uncompressed and BC7)
        auto texData = resource::TextureResource::loadTexture(path);
        if (texData.mipData.empty())
            return;

        const auto& mip0 = texData.mipData[0];
        sourceWidth = mip0.width;
        sourceHeight = mip0.height;

        if (texData.compressionFormat != resource::TextureCompressionFormat::Uncompressed)
        {
            // Decompress BC7 to RGBA
            sourcePixels = resource::BC7Decoder::decompress(mip0.data.data(), mip0.width, mip0.height);
        }
        else
        {
            // Already RGBA from TGAReader
            sourcePixels = mip0.data;
        }

        uploadOriginalToGPU();
        processDirty = true;
    }

    void BackgroundRemovalWindow::processImage()
    {
        if (sourcePixels.empty())
            return;

        lastResult = imageprocessing::BackgroundRemover::process(
            sourcePixels.data(), sourceWidth, sourceHeight, params);

        if (lastResult.valid())
        {
            // Composite result over checkerboard for preview
            std::vector<uint8_t> previewData(lastResult.rgbaData.size());
            for (size_t i = 0; i < static_cast<size_t>(lastResult.width) * lastResult.height; ++i)
            {
                size_t idx = i * 4;
                uint32_t px = static_cast<uint32_t>(i % lastResult.width);
                uint32_t py = static_cast<uint32_t>(i / lastResult.width);
                bool dark = ((px / 16) + (py / 16)) % 2 == 0;
                uint8_t checker = dark ? 40 : 60;

                float alpha = lastResult.rgbaData[idx + 3] / 255.0f;
                previewData[idx + 0] = static_cast<uint8_t>(lastResult.rgbaData[idx + 0] * alpha + checker * (1.0f - alpha));
                previewData[idx + 1] = static_cast<uint8_t>(lastResult.rgbaData[idx + 1] * alpha + checker * (1.0f - alpha));
                previewData[idx + 2] = static_cast<uint8_t>(lastResult.rgbaData[idx + 2] * alpha + checker * (1.0f - alpha));
                previewData[idx + 3] = 255;
            }

            // Upload composited preview to GPU
            if (resultHandle.isValid())
            {
                auto& dispatcher = events::EventDispatcher::instance();
                events::render::ReleaseEditorTextureCommand releaseCmd;
                releaseCmd.handle = resultHandle.imguiDescriptorSet;
                dispatcher.execute(releaseCmd);
                resultHandle = {};
            }

            resource::TextureData texData;
            texData.width = lastResult.width;
            texData.height = lastResult.height;
            texData.numbersOfChannels = 4;
            texData.mipLevels = 1;
            texData.compressionFormat = resource::TextureCompressionFormat::Uncompressed;

            resource::MipLevelData mip;
            mip.width = lastResult.width;
            mip.height = lastResult.height;
            mip.dataSize = static_cast<uint32_t>(previewData.size());
            mip.data = std::move(previewData);
            texData.mipData.push_back(std::move(mip));

            auto& dispatcher = events::EventDispatcher::instance();
            events::render::LoadEditorTextureFromDataCommand cmd;
            cmd.textureData = std::move(texData);
            resultHandle = dispatcher.execute(cmd);
        }
    }

    void BackgroundRemovalWindow::uploadOriginalToGPU()
    {
        if (originalHandle.isValid())
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::render::ReleaseEditorTextureCommand releaseCmd;
            releaseCmd.handle = originalHandle.imguiDescriptorSet;
            dispatcher.execute(releaseCmd);
            originalHandle = {};
        }

        if (sourcePixels.empty())
            return;

        resource::TextureData texData;
        texData.width = sourceWidth;
        texData.height = sourceHeight;
        texData.numbersOfChannels = 4;
        texData.mipLevels = 1;
        texData.compressionFormat = resource::TextureCompressionFormat::Uncompressed;

        resource::MipLevelData mip;
        mip.width = sourceWidth;
        mip.height = sourceHeight;
        mip.dataSize = static_cast<uint32_t>(sourcePixels.size());
        mip.data = sourcePixels;
        texData.mipData.push_back(std::move(mip));

        auto& dispatcher = events::EventDispatcher::instance();
        events::render::LoadEditorTextureFromDataCommand cmd;
        cmd.textureData = std::move(texData);
        originalHandle = dispatcher.execute(cmd);
    }

    void BackgroundRemovalWindow::releaseTextures()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (originalHandle.isValid())
        {
            events::render::ReleaseEditorTextureCommand cmd;
            cmd.handle = originalHandle.imguiDescriptorSet;
            dispatcher.execute(cmd);
            originalHandle = {};
        }
        if (resultHandle.isValid())
        {
            events::render::ReleaseEditorTextureCommand cmd;
            cmd.handle = resultHandle.imguiDescriptorSet;
            dispatcher.execute(cmd);
            resultHandle = {};
        }
    }

    void BackgroundRemovalWindow::startExport()
    {
        exporting = true;
        exportStatusMessage.clear();

        auto result = lastResult;
        auto path = exportPath;

        // Ensure .vfImage extension
        if (std::filesystem::path(path).extension() != ".vfImage")
            path += ".vfImage";

        exportFuture = std::async(std::launch::async,
            [result = std::move(result), path]()
            {
                bool ok = imageprocessing::BackgroundRemover::saveAsVFImage(result, path);
                if (ok)
                {
                    // Generate .vfmeta
                    auto metaPath = asset::AssetMetadataSerializer::getMetaPath(path);
                    asset::AssetMetadata metadata;
                    metadata.guid = asset::AssetGUID::generate();
                    metadata.type = resource::AssetType::Texture;
                    metadata.importSourcePath = "imageprocessing://background-removal";
                    metadata.formatVersion = 1;

                    auto time = std::time(nullptr);
                    std::tm tm{};
#ifdef _WIN32
                    localtime_s(&tm, &time);
#else
                    localtime_r(&time, &tm);
#endif
                    char buf[32];
                    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
                    metadata.importTimestamp = buf;

                    asset::AssetMetadataSerializer::save(metadata, metaPath);
                }
                return ok;
            });
    }

    void BackgroundRemovalWindow::pollExport()
    {
        if (!exportFuture.valid())
            return;

        auto status = exportFuture.wait_for(std::chrono::milliseconds(0));
        if (status != std::future_status::ready)
            return;

        bool success = exportFuture.get();
        exporting = false;

        if (success)
            exportStatusMessage = "Exported: " + exportPath;
        else
            exportStatusMessage = "Export failed!";
    }
}
