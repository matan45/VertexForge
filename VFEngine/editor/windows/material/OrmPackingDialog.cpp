#include "print/Log.hpp"
#include "OrmPackingDialog.hpp"
#include <texture/OrmTexturePacker.hpp>
#include <nfd/FileDialog.hpp>
#include <events/EventDispatcher.hpp>
#include <events/project/ResourceEvents.hpp>
#include "TextureCompressor.hpp"
#include "imgui.h"
#include <filesystem>

namespace editor::materialeditor
{
    void OrmPackingDialog::open()
    {
        showDialog = true;
        resetState();
    }

    void OrmPackingDialog::resetState()
    {
        aoPath.clear();
        roughnessPath.clear();
        metallicPath.clear();
        heightPath.clear();
        outputPath.clear();
        errorMessage.clear();
        progress = 0.0f;
        packInProgress = false;
    }

    void OrmPackingDialog::draw()
    {
        if (!showDialog) return;

        ImGui::OpenPopup("Pack ORM Texture");

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(500, 350), ImGuiCond_FirstUseEver);

        if (ImGui::BeginPopupModal("Pack ORM Texture", &showDialog, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped(
                "Pack textures into a single ORM texture (R=AO, G=Roughness, B=Metallic, A=Height). All textures are optional - provide at least one. Missing textures use defaults.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            nfd::FileDialog fileDialog;
            std::vector<std::pair<std::wstring, std::wstring>> filters = {
                {L"VF Image", L"*.vfImage"},
                {L"All Files", L"*.*"}
            };

            // AO texture selection
            ImGui::Text("Ambient Occlusion (AO) - default: 255 (no occlusion):");
            ImGui::PushID("ao");
            {
                std::string display = aoPath.empty()
                                          ? "(None - uses default)"
                                          : std::filesystem::path(aoPath).filename().string();
                ImGui::InputText("##path", &display[0], display.size(), ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine();
                if (ImGui::Button("Browse..."))
                {
                    std::string path = fileDialog.openFileDialog(filters);
                    if (!path.empty()) aoPath = path;
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear")) aoPath.clear();
            }
            ImGui::PopID();

            // Roughness texture selection
            ImGui::Text("Roughness - default: 128 (mid roughness):");
            ImGui::PushID("roughness");
            {
                std::string display = roughnessPath.empty()
                                          ? "(None - uses default)"
                                          : std::filesystem::path(roughnessPath).filename().string();
                ImGui::InputText("##path", &display[0], display.size(), ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine();
                if (ImGui::Button("Browse..."))
                {
                    std::string path = fileDialog.openFileDialog(filters);
                    if (!path.empty()) roughnessPath = path;
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear")) roughnessPath.clear();
            }
            ImGui::PopID();

            // Metallic texture selection
            ImGui::Text("Metallic - default: 0 (non-metallic):");
            ImGui::PushID("metallic");
            {
                std::string display = metallicPath.empty()
                                          ? "(None - uses default)"
                                          : std::filesystem::path(metallicPath).filename().string();
                ImGui::InputText("##path", &display[0], display.size(), ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine();
                if (ImGui::Button("Browse..."))
                {
                    std::string path = fileDialog.openFileDialog(filters);
                    if (!path.empty()) metallicPath = path;
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear")) metallicPath.clear();
            }
            ImGui::PopID();

            // VK-1609: height texture selection. Terrain height-blended layer compositing reads
            // per-layer height from ORM alpha, which is why it costs no extra texture fetch.
            ImGui::Text("Height - default: 128 (neutral, no height blending):");
            ImGui::PushID("height");
            {
                std::string display = heightPath.empty()
                                          ? "(None - uses default)"
                                          : std::filesystem::path(heightPath).filename().string();
                ImGui::InputText("##path", &display[0], display.size(), ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine();
                if (ImGui::Button("Browse..."))
                {
                    std::string path = fileDialog.openFileDialog(filters);
                    if (!path.empty())
                    {
                        heightPath = path;
                        // The vendored BC7 encoder emits mode 6 only - one 4-bit index shared
                        // across RGBA - so a height signal uncorrelated with AO/roughness/metallic
                        // quantizes badly AND drags RGB down with it. Steer to Uncompressed by
                        // default; the artist can still override.
                        compressionMode = importConfig::TextureCompressionMode::Uncompressed;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear")) heightPath.clear();
            }
            ImGui::PopID();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Output path selection
            ImGui::Text("Output Path:");
            ImGui::PushID("output");
            {
                std::string display = outputPath.empty()
                                          ? "(Select output location)"
                                          : std::filesystem::path(outputPath).filename().string();
                ImGui::InputText("##path", &display[0], display.size(), ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine();
                if (ImGui::Button("Browse..."))
                {
                    std::vector<std::pair<std::wstring, std::wstring>> saveFilters = {
                        {L"VF Image", L"*.vfImage"}
                    };
                    std::string path = fileDialog.saveFileDialog(saveFilters, L"ORM_packed.vfImage");
                    if (!path.empty())
                    {
                        if (path.find(".vfImage") == std::string::npos)
                        {
                            path += ".vfImage";
                        }
                        outputPath = path;
                    }
                }
            }
            ImGui::PopID();

            ImGui::Spacing();

            // Progress bar
            if (packInProgress)
            {
                ImGui::ProgressBar(progress, ImVec2(-1, 0), "Packing...");
            }

            // Error message
            if (!errorMessage.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", errorMessage.c_str());
            }

            ImGui::Spacing();

            // Compression settings
            ImGui::Text("Output Compression:");
            ImGui::Indent();
            {
                const char* modeNames[] = {"Uncompressed", "BC (BC7)"};
                int modeIndex = static_cast<int>(compressionMode);
                if (ImGui::Combo("Mode", &modeIndex, modeNames, IM_ARRAYSIZE(modeNames)))
                {
                    compressionMode = static_cast<importConfig::TextureCompressionMode>(modeIndex);
                }

                if (compressionMode != importConfig::TextureCompressionMode::Uncompressed)
                {
                    const char* qualityNames[] = {"Fast", "Balanced", "Quality"};
                    int qualityIndex = static_cast<int>(compressionQuality);
                    if (ImGui::Combo("Quality", &qualityIndex, qualityNames, IM_ARRAYSIZE(qualityNames)))
                    {
                        compressionQuality = static_cast<importConfig::TextureCompressionQuality>(qualityIndex);
                    }

                    if (!heightPath.empty())
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                                           "BC7 shares one index across RGBA - a height channel\n"
                                           "uncorrelated with AO/roughness/metallic will band and\n"
                                           "degrade RGB. Prefer Uncompressed for height-bearing ORM.");
                    }
                }
            }
            ImGui::Unindent();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Buttons
            bool hasAtLeastOneTexture = !aoPath.empty() || !roughnessPath.empty() || !metallicPath.empty()
                                        || !heightPath.empty();
            bool canPack = hasAtLeastOneTexture && !outputPath.empty() && !packInProgress;

            if (!canPack) ImGui::BeginDisabled();
            if (ImGui::Button("Pack", ImVec2(120, 0)))
            {
                doPack();
            }
            if (!canPack) ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                showDialog = false;
            }

            ImGui::EndPopup();
        }
    }

    void OrmPackingDialog::doPack()
    {
        errorMessage.clear();
        packInProgress = true;
        progress = 0.0f;

        texture::OrmPackInput input;
        input.aoPath = aoPath;
        input.roughnessPath = roughnessPath;
        input.metallicPath = metallicPath;
        input.heightPath = heightPath;
        input.outputPath = outputPath;

        // Decompress compressed inputs (BC7) before channel packing
        input.decompressCallback = [](resource::TextureData& textureData) -> bool
        {
            return types::TextureCompressor::decompressAllMips(textureData);
        };

        // Set up compression callback if compression is enabled.
        // Only quality is captured — mode is only used here to gate whether compression
        // happens at all. When enabled, ORM textures are always BC7 (LDR data).
        auto quality = compressionQuality;
        if (compressionMode != importConfig::TextureCompressionMode::Uncompressed)
        {
            input.compressCallback = [quality](resource::TextureData& textureData)
            {
                textureData.compressionFormat = resource::TextureCompressionFormat::BC7;

                for (auto& mip : textureData.mipData)
                {
                    auto compressed = types::TextureCompressor::compressBC7(
                        mip.data.data(), mip.width, mip.height, quality);

                    if (!compressed.empty())
                    {
                        mip.dataSize = static_cast<uint32_t>(compressed.size());
                        mip.data = std::move(compressed);
                    }
                }
            };
        }

        auto result = texture::OrmTexturePacker::packORM(
            input,
            [this](float p)
            {
                progress = p;
            }
        );

        packInProgress = false;

        if (result.success)
        {
            vfLogInfo("ORM texture packed successfully: {}", result.outputPath);

            showDialog = false;

            events::resource::ImportCompletedNotification notification;
            services::ImportResult importResult;
            importResult.success = true;
            importResult.sourcePath = result.outputPath;
            importResult.outputPath = result.outputPath;
            importResult.assetType = resource::AssetType::Texture;
            notification.results.push_back(importResult);
            events::EventDispatcher::instance().publish(notification);
        }
        else
        {
            errorMessage = result.errorMessage;
            vfLogError("Failed to pack ORM texture: {}", result.errorMessage);
        }
    }
}
