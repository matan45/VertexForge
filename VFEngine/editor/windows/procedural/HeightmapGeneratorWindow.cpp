#include "HeightmapGeneratorWindow.hpp"
#include "events/render/RenderEvents.hpp"
#include "resource/Types.hpp"
#include <imgui.h>
#include "asset/AssetMetadataSerializer.hpp"
#include "asset/AssetMetadata.hpp"
#include <random>
#include <chrono>
#include <filesystem>
#include <ctime>

namespace windows
{
    HeightmapGeneratorWindow::~HeightmapGeneratorWindow()
    {
        if (generationFuture.valid())
            generationFuture.wait();
        if (exportFuture.valid())
            exportFuture.wait();
        releasePreviewTexture();
    }

    void HeightmapGeneratorWindow::show()
    {
        visible = true;
        previewDirty = true;
    }

    void HeightmapGeneratorWindow::draw()
    {
        if (!visible)
            return;

        // Poll async generation and export
        if (generating)
            pollGeneration();
        if (exporting)
            pollExport();

        // Update preview BEFORE rendering so the descriptor set is valid for ImGui::Image
        if (previewDirty && !generating)
        {
            float currentTime = static_cast<float>(ImGui::GetTime());
            if (currentTime - lastPreviewTime >= previewDebounceTime)
            {
                updatePreview();
                previewDirty = false;
                lastPreviewTime = currentTime;
            }
        }

        ImGui::SetNextWindowSize(ImVec2(700, 550), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Generate Heightmap", &visible))
        {
            // Left side: parameters
            ImGui::BeginChild("Params", ImVec2(380, 0), true);
            drawParameterControls();
            ImGui::Separator();
            drawExportSection();
            ImGui::EndChild();

            ImGui::SameLine();

            // Right side: preview
            ImGui::BeginChild("Preview", ImVec2(0, 0), true);
            drawPreview();
            ImGui::EndChild();
        }
        ImGui::End();
    }

    void HeightmapGeneratorWindow::drawParameterControls()
    {
        // Presets
        const char* presets[] = {
            "Custom",
            "Flat Hills",
            "Rolling Plains",
            "Mountains",
            "Sharp Peaks",
            "Deep Valleys",
            "Plateaus",
            "Islands"
        };
        if (ImGui::Combo("Preset", &presetIndex, presets, 8))
        {
            if (presetIndex != 0)
            {
                applyPreset(presetIndex);
                previewDirty = true;
            }
        }

        ImGui::Separator();
        ImGui::Text("Noise Parameters");
        ImGui::Separator();

        // Resolution
        const char* resolutions[] = { "1024", "2048", "4096", "8192", "16384" };
        if (ImGui::Combo("Resolution", &resolutionIndex, resolutions, 5))
            previewDirty = true;

        // Noise type
        const char* noiseTypes[] = { "Perlin", "Simplex" };
        if (ImGui::Combo("Noise Type", &noiseTypeIndex, noiseTypes, 2))
        { previewDirty = true; presetIndex = 0; }

        // Fractal type
        const char* fractalTypes[] = { "None", "FBM", "Ridged", "Billowy" };
        if (ImGui::Combo("Fractal Type", &fractalTypeIndex, fractalTypes, 4))
        { previewDirty = true; presetIndex = 0; }

        // Octaves (only for fractal types)
        if (fractalTypeIndex != 0)
        {
            if (ImGui::SliderInt("Octaves", &params.octaves, 1, 16))
            { previewDirty = true; presetIndex = 0; }
        }

        if (ImGui::DragFloat("Frequency", &params.frequency, 0.0001f, 0.001f, 0.1f, "%.4f"))
        { previewDirty = true; presetIndex = 0; }

        if (ImGui::SliderFloat("Amplitude", &params.amplitude, 0.0f, 1.0f))
        { previewDirty = true; presetIndex = 0; }

        if (fractalTypeIndex != 0)
        {
            if (ImGui::DragFloat("Lacunarity", &params.lacunarity, 0.01f, 1.0f, 4.0f, "%.2f"))
            { previewDirty = true; presetIndex = 0; }

            if (ImGui::SliderFloat("Persistence", &params.persistence, 0.0f, 1.0f))
            { previewDirty = true; presetIndex = 0; }
        }

        // Seed
        ImGui::Separator();
        if (ImGui::InputScalar("Seed", ImGuiDataType_U32, &params.seed))
            previewDirty = true;

        ImGui::SameLine();
        if (ImGui::Button("Randomize"))
        {
            std::random_device rd;
            params.seed = rd();
            previewDirty = true;
        }

        // Domain warping
        ImGui::Separator();
        if (ImGui::Checkbox("Domain Warping", &params.domainWarp.enabled))
            previewDirty = true;

        if (params.domainWarp.enabled)
        {
            if (ImGui::DragFloat("Warp Strength", &params.domainWarp.amplitude, 0.5f, 0.0f, 200.0f, "%.1f"))
                previewDirty = true;

            if (ImGui::DragFloat("Warp Frequency", &params.domainWarp.frequency, 0.0001f, 0.001f, 0.05f, "%.4f"))
                previewDirty = true;
        }

        // Post-processing
        ImGui::Separator();
        ImGui::Text("Post-Processing");

        if (ImGui::DragFloat("Height Curve", &params.heightExponent, 0.05f, 0.1f, 5.0f, "%.2f"))
            previewDirty = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(">1 = deeper valleys, sharper peaks\n<1 = flatter plateaus");

        if (ImGui::Checkbox("Invert", &params.invert))
            previewDirty = true;

        if (ImGui::Checkbox("Terracing", &params.terracing))
            previewDirty = true;

        if (params.terracing)
        {
            if (ImGui::SliderInt("Terrace Steps", &params.terraceSteps, 2, 64))
                previewDirty = true;
        }
    }

    void HeightmapGeneratorWindow::drawPreview()
    {
        ImGui::Text("Preview (256x256)");
        ImGui::Separator();

        if (previewHandle.isValid())
        {
            ImVec2 available = ImGui::GetContentRegionAvail();
            float previewSize = std::min(available.x, available.y - 20.0f);
            previewSize = std::max(previewSize, 128.0f);
            ImGui::Image(previewHandle.imguiDescriptorSet, ImVec2(previewSize, previewSize));
        }
        else
        {
            ImGui::TextDisabled("No preview available");
        }
    }

    void HeightmapGeneratorWindow::drawExportSection()
    {
        ImGui::Text("Export");
        ImGui::Separator();

        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Format: Uncompressed .vfImage");

        // Output path
        if (ImGui::Button("Browse..."))
        {
            std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
                {L"VF Image (*.vfImage)", L"*.vfImage"}
            };
            std::string path = fileDialog.saveFileDialog(fileTypes, L"vfImage");
            if (!path.empty())
                exportPath = path;
        }
        ImGui::SameLine();
        ImGui::TextWrapped("%s", exportPath.empty() ? "(no path selected)" : exportPath.c_str());

        // Progress bar during generation
        if (generating)
        {
            float progress = generationProgress.load();
            ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
            ImGui::TextDisabled("Generating...");
        }
        else if (exporting)
        {
            ImGui::TextDisabled("Exporting...");
        }
        else
        {
            // Generate & Export button
            bool canExport = !exportPath.empty() && !generating && !exporting;
            ImGui::BeginDisabled(!canExport);
            if (ImGui::Button("Generate & Export", ImVec2(-1.0f, 30.0f)))
            {
                startGeneration();
            }
            ImGui::EndDisabled();
        }

        // Status message
        if (!exportStatusMessage.empty())
        {
            ImGui::TextWrapped("%s", exportStatusMessage.c_str());
        }
    }

    void HeightmapGeneratorWindow::updatePreview()
    {
        syncParamsFromUI();

        auto result = procedural::HeightmapGenerator::generatePreview(params);
        if (!result.valid())
            return;

        // Build TextureData for GPU upload
        resource::TextureData texData;
        texData.width = result.width;
        texData.height = result.height;
        texData.numbersOfChannels = 4;
        texData.mipLevels = 1;
        texData.compressionFormat = resource::TextureCompressionFormat::Uncompressed;

        resource::MipLevelData mip;
        mip.width = result.width;
        mip.height = result.height;
        mip.dataSize = static_cast<uint32_t>(result.rgbaData.size());
        mip.data = std::move(result.rgbaData);
        texData.mipData.push_back(std::move(mip));

        // Release old texture
        releasePreviewTexture();

        // Upload to GPU
        auto& dispatcher = events::EventDispatcher::instance();
        events::render::LoadEditorTextureFromDataCommand cmd;
        cmd.textureData = std::move(texData);
        previewHandle = dispatcher.execute(cmd);
    }

    void HeightmapGeneratorWindow::releasePreviewTexture()
    {
        if (previewHandle.isValid())
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::render::ReleaseEditorTextureCommand releaseCmd;
            releaseCmd.handle = previewHandle.imguiDescriptorSet;
            dispatcher.execute(releaseCmd);
            previewHandle = {};
        }
    }

    void HeightmapGeneratorWindow::startGeneration()
    {
        syncParamsFromUI();
        generating = true;
        generationProgress.store(0.0f);
        exportStatusMessage.clear();

        auto genParams = params;
        auto* progressPtr = &generationProgress;

        generationFuture = std::async(std::launch::async,
            [genParams, progressPtr]()
            {
                return procedural::HeightmapGenerator::generate(genParams,
                    [progressPtr](float p) { progressPtr->store(p); });
            });
    }

    void HeightmapGeneratorWindow::pollGeneration()
    {
        if (!generationFuture.valid())
            return;

        auto status = generationFuture.wait_for(std::chrono::milliseconds(0));
        if (status != std::future_status::ready)
            return;

        lastResult = generationFuture.get();
        generating = false;

        if (lastResult.valid())
        {
            startExport();
        }
        else
        {
            exportStatusMessage = "Generation failed!";
        }
    }

    void HeightmapGeneratorWindow::startExport()
    {
        exporting = true;
        exportStatusMessage.clear();

        std::string basePath = exportPath;
        // Remove extension if present
        auto ext = std::filesystem::path(basePath).extension().string();
        if (ext == ".vfImage")
        {
            basePath = std::filesystem::path(basePath).replace_extension().string();
        }

        pendingExportPath = basePath + ".vfImage";

        // Capture result by value for the async task
        auto resultCopy = lastResult;
        auto path = pendingExportPath;

        exportFuture = std::async(std::launch::async,
            [resultCopy = std::move(resultCopy), path]()
            {
                bool ok = procedural::HeightmapGenerator::saveAsVFImage(resultCopy, path);
                if (ok)
                {
                    // Generate .vfmeta sidecar
                    auto metaPath = asset::AssetMetadataSerializer::getMetaPath(path);
                    asset::AssetMetadata metadata;
                    metadata.guid = asset::AssetGUID::generate();
                    metadata.type = resource::AssetType::Texture;
                    metadata.importSourcePath = "procedural://heightmap";
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

    void HeightmapGeneratorWindow::pollExport()
    {
        if (!exportFuture.valid())
            return;

        auto status = exportFuture.wait_for(std::chrono::milliseconds(0));
        if (status != std::future_status::ready)
            return;

        bool success = exportFuture.get();
        exporting = false;

        if (success)
        {
            exportStatusMessage = "Exported: " + pendingExportPath;
        }
        else
        {
            exportStatusMessage = "Export failed!";
        }
    }

    void HeightmapGeneratorWindow::applyPreset(int preset)
    {
        // Reset post-processing
        params.domainWarp.enabled = false;
        params.invert = false;
        params.terracing = false;
        params.terraceSteps = 8;

        switch (preset)
        {
        case 1: // Flat Hills — gentle rolling terrain
            noiseTypeIndex = 1;  // Simplex
            fractalTypeIndex = 1; // FBM
            params.octaves = 4;
            params.frequency = 0.002f;
            params.amplitude = 0.6f;
            params.lacunarity = 2.0f;
            params.persistence = 0.35f;
            params.heightExponent = 0.7f;
            break;

        case 2: // Rolling Plains — wide open terrain with mild variation
            noiseTypeIndex = 0;  // Perlin
            fractalTypeIndex = 1; // FBM
            params.octaves = 6;
            params.frequency = 0.003f;
            params.amplitude = 0.8f;
            params.lacunarity = 2.2f;
            params.persistence = 0.4f;
            params.heightExponent = 0.6f;
            params.domainWarp.enabled = true;
            params.domainWarp.amplitude = 30.0f;
            params.domainWarp.frequency = 0.003f;
            break;

        case 3: // Mountains — dramatic terrain with peaks
            noiseTypeIndex = 1;  // Simplex
            fractalTypeIndex = 2; // Ridged
            params.octaves = 8;
            params.frequency = 0.004f;
            params.amplitude = 1.0f;
            params.lacunarity = 2.2f;
            params.persistence = 0.5f;
            params.heightExponent = 1.4f;
            params.domainWarp.enabled = true;
            params.domainWarp.amplitude = 60.0f;
            params.domainWarp.frequency = 0.004f;
            break;

        case 4: // Sharp Peaks — aggressive jagged mountains
            noiseTypeIndex = 0;  // Perlin
            fractalTypeIndex = 2; // Ridged
            params.octaves = 10;
            params.frequency = 0.006f;
            params.amplitude = 1.0f;
            params.lacunarity = 2.5f;
            params.persistence = 0.55f;
            params.heightExponent = 2.0f;
            break;

        case 5: // Deep Valleys — inverted ridged for canyon-like terrain
            noiseTypeIndex = 1;  // Simplex
            fractalTypeIndex = 2; // Ridged
            params.octaves = 8;
            params.frequency = 0.004f;
            params.amplitude = 1.0f;
            params.lacunarity = 2.0f;
            params.persistence = 0.5f;
            params.heightExponent = 1.5f;
            params.invert = true;
            params.domainWarp.enabled = true;
            params.domainWarp.amplitude = 40.0f;
            params.domainWarp.frequency = 0.003f;
            break;

        case 6: // Plateaus — flat-topped mesa terrain
            noiseTypeIndex = 0;  // Perlin
            fractalTypeIndex = 1; // FBM
            params.octaves = 5;
            params.frequency = 0.003f;
            params.amplitude = 0.8f;
            params.lacunarity = 2.0f;
            params.persistence = 0.45f;
            params.heightExponent = 0.4f;
            params.terracing = true;
            params.terraceSteps = 6;
            break;

        case 7: // Islands — smooth rounded landmasses with low areas
            noiseTypeIndex = 1;  // Simplex
            fractalTypeIndex = 3; // Billowy
            params.octaves = 6;
            params.frequency = 0.003f;
            params.amplitude = 0.9f;
            params.lacunarity = 2.0f;
            params.persistence = 0.4f;
            params.heightExponent = 1.8f;
            params.domainWarp.enabled = true;
            params.domainWarp.amplitude = 50.0f;
            params.domainWarp.frequency = 0.002f;
            break;
        }
    }

    void HeightmapGeneratorWindow::syncParamsFromUI()
    {
        // Resolution
        constexpr uint32_t resolutionValues[] = { 1024, 2048, 4096, 8192, 16384 };
        params.width = resolutionValues[resolutionIndex];
        params.height = resolutionValues[resolutionIndex];

        // Noise type
        params.noiseType = static_cast<procedural::NoiseType>(noiseTypeIndex);

        // Fractal type
        params.fractalType = static_cast<procedural::FractalType>(fractalTypeIndex);
    }
}
