#include "TerrainMaterialEditorWindow.hpp"
#include "../../graph/ShaderGraphCompiler.hpp"
#include <terrain/TerrainMaterialAsset.hpp>
#include <resource/ResourceManager.hpp>
#include "events/EventDispatcher.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "events/project/ResourceEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
#include "imgui.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <format>
#include <cstring>

namespace windows
{
    TerrainMaterialEditorWindow::TerrainMaterialEditorWindow(const std::string& materialPath)
        : materialPath(materialPath)
    {
        std::filesystem::path path(materialPath);
        windowTitle = "Terrain Material: " + path.filename().string();
    }

    void TerrainMaterialEditorWindow::loadMaterial()
    {
        materialData = resource::ResourceManager::loadTerrainMaterial(asset::AssetRef::fromPath(materialPath));

        if (!materialData)
        {
            vfLogInfo("Creating new terrain material: {}", materialPath);
            std::filesystem::path path(materialPath);
            auto newMaterial = terrain::TerrainMaterialAsset::createDefault(path.stem().string());
            materialData = std::make_shared<terrain::TerrainMaterialData>(std::move(newMaterial));
        }
    }

    void TerrainMaterialEditorWindow::saveMaterial()
    {
        if (!materialData) return;

        compileMaterial();

        if (terrain::TerrainMaterialAsset::save(materialPath, *materialData))
        {
            isDirty = false;
            vfLogInfo("Terrain material saved: {}", materialPath);
            events::resource::AssetSavedNotification assetNotif;
            assetNotif.filePath = materialPath;
            events::EventDispatcher::instance().publish(assetNotif);
        }
        else
        {
            vfLogError("Failed to save terrain material: {}", materialPath);
        }
    }

    void TerrainMaterialEditorWindow::removeLayer(int removeIndex, int currentCount)
    {
        for (int i = removeIndex; i < currentCount - 1; ++i)
        {
            materialData->layers[i] = materialData->layers[i + 1];
        }
        materialData->layers[currentCount - 1] = terrain::TerrainMaterialLayer{};
        materialData->activeLayerCount = static_cast<uint8_t>(currentCount - 1);
    }

    void TerrainMaterialEditorWindow::compileMaterial()
    {
        if (!materialData) return;

        auto result = editor::graph::ShaderGraphCompiler::compileTerrainMaterial(*materialData);

        if (result.success)
        {
            materialData->cachedMaterialSnippet = result.materialSnippet;
            materialData->needsRecompile = false;
            showCompileError = false;
            vfLogInfo("Terrain material compiled successfully: {}", materialData->name);

            std::filesystem::path snippetPath = "../../resources/shaders/material/terrain_material_generated.glsl";
            std::ofstream file(snippetPath);
            if (file.is_open())
            {
                file << "// Generated terrain material shader code\n";
                file << result.materialSnippet;
                file.close();
                vfLogInfo("Terrain material shader written to: {}", snippetPath.string());
            }
            else
            {
                vfLogError("Failed to write terrain material shader file");
            }

            events::EventDispatcher::instance().publish(
                events::terrain::TerrainMaterialCompiledNotification{});
        }
        else
        {
            showCompileError = true;
            compileErrorMessage = result.errorMessage;
            vfLogError("Terrain material compilation failed: {}", result.errorMessage);
        }
    }

    void TerrainMaterialEditorWindow::onChanged()
    {
        isDirty = true;
        materialData->needsRecompile = true;
    }

    void TerrainMaterialEditorWindow::draw()
    {
        if (!isOpen) return;

        if (!materialData)
        {
            loadMaterial();
        }

        ImGui::SetNextWindowSize(ImVec2(600, 700), ImGuiCond_FirstUseEver);

        std::string title = windowTitle + (isDirty ? " *" : "  ");

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar;
        if (ImGui::Begin(title.c_str(), &isOpen, flags))
        {
            if (isOpen)
            {
                drawToolbar();
                drawLayerProperties();
            }
        }
        ImGui::End();
    }

    void TerrainMaterialEditorWindow::drawToolbar()
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save", "Ctrl+S"))
                {
                    saveMaterial();
                }
                if (ImGui::MenuItem("Compile", "F5"))
                {
                    compileMaterial();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Close"))
                {
                    isOpen = false;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        if (ImGui::Button("Save"))
        {
            saveMaterial();
        }
        ImGui::SameLine();
        if (ImGui::Button("Compile"))
        {
            compileMaterial();
        }

        ImGui::SameLine();
        ImGui::PushItemWidth(120);
        if (showCompileError)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Compile Error!");
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(compileErrorMessage.c_str());
                ImGui::EndTooltip();
            }
        }
        else if (materialData && !materialData->needsRecompile)
        {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Compiled      ");
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Needs Compile");
        }
        ImGui::PopItemWidth();

        ImGui::Separator();
    }

    void TerrainMaterialEditorWindow::drawLayerProperties()
    {
        if (!materialData)
        {
            ImGui::TextDisabled("No material loaded");
            return;
        }

        int layerCount = materialData->activeLayerCount;

        ImGui::Text("Layers: %d / %d", layerCount, terrain::MAX_TERRAIN_LAYERS);
        ImGui::SameLine();
        if (layerCount < terrain::MAX_TERRAIN_LAYERS)
        {
            if (ImGui::SmallButton("+ Add Layer"))
            {
                auto& newLayer = materialData->layers[layerCount];
                newLayer = terrain::TerrainMaterialLayer{};
                newLayer.name = "Layer " + std::to_string(layerCount);
                layerCount++;
                materialData->activeLayerCount = static_cast<uint8_t>(layerCount);
                onChanged();
            }
        }
        else
        {
            ImGui::BeginDisabled();
            ImGui::SmallButton("+ Add Layer");
            ImGui::EndDisabled();
        }

        ImGui::Spacing();
        ImGui::Separator();

        for (int i = 0; i < layerCount; ++i)
        {
            auto& layer = materialData->layers[i];

            ImGui::PushID(i);

            if (ImGui::Checkbox("##enabled", &layer.enabled))
            {
                onChanged();
            }
            ImGui::SameLine();

            std::string headerLabel = std::format("{} ({})", layer.name, i);
            if (ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent(8.0f);

                {
                    char nameBuffer[64];
                    std::strncpy(nameBuffer, layer.name.c_str(), sizeof(nameBuffer) - 1);
                    nameBuffer[sizeof(nameBuffer) - 1] = '\0';
                    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
                    {
                        layer.name = std::string(nameBuffer);
                        onChanged();
                    }
                }

                if (i > 0)
                {
                    const char* blendModes[] = {"Linear", "Overlay"};
                    int currentBlend = (layer.blendMode == terrain::TerrainLayerBlendMode::Overlay) ? 1 : 0;

                    if (ImGui::Combo("Blend Mode", &currentBlend, blendModes, IM_ARRAYSIZE(blendModes)))
                    {
                        layer.blendMode = (currentBlend == 1)
                            ? terrain::TerrainLayerBlendMode::Overlay
                            : terrain::TerrainLayerBlendMode::Linear;
                        onChanged();
                    }
                }

                {
                    ImGui::Text("Albedo:");
                    ImGui::SameLine();
                    std::string displayPath = !layer.albedoTextureRef.isValid() ? "(None)" :
                        std::filesystem::path(layer.albedoTextureRef.resolve()).filename().string();
                    ImGui::TextDisabled("%s", displayPath.c_str());

                    ImGui::SameLine();
                    if (ImGui::SmallButton("Browse##albedo"))
                    {
                        nfd::FileDialog fileDialog;
                        std::vector<std::pair<std::wstring, std::wstring>> filters = {
                            {L"VF Image", L"*.vfImage"}
                        };
                        std::string selectedPath = fileDialog.openFileDialog(filters);
                        if (!selectedPath.empty())
                        {
                            selectedPath.erase(
                                std::remove(selectedPath.begin(), selectedPath.end(), '\0'),
                                selectedPath.end());
                            layer.albedoTextureRef = asset::AssetRef::fromPath(selectedPath);
                            onChanged();
                        }
                    }
                    if (layer.albedoTextureRef.isValid())
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X##albedo"))
                        {
                            layer.albedoTextureRef = asset::AssetRef::invalid();
                            onChanged();
                        }
                    }
                }

                {
                    ImGui::Text("Normal:");
                    ImGui::SameLine();
                    std::string displayPath = !layer.normalTextureRef.isValid() ? "(None)" :
                        std::filesystem::path(layer.normalTextureRef.resolve()).filename().string();
                    ImGui::TextDisabled("%s", displayPath.c_str());

                    ImGui::SameLine();
                    if (ImGui::SmallButton("Browse##normal"))
                    {
                        nfd::FileDialog fileDialog;
                        std::vector<std::pair<std::wstring, std::wstring>> filters = {
                            {L"VF Image", L"*.vfImage"}
                        };
                        std::string selectedPath = fileDialog.openFileDialog(filters);
                        if (!selectedPath.empty())
                        {
                            selectedPath.erase(
                                std::remove(selectedPath.begin(), selectedPath.end(), '\0'),
                                selectedPath.end());
                            layer.normalTextureRef = asset::AssetRef::fromPath(selectedPath);
                            onChanged();
                        }
                    }
                    if (layer.normalTextureRef.isValid())
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X##normal"))
                        {
                            layer.normalTextureRef = asset::AssetRef::invalid();
                            onChanged();
                        }
                    }
                }

                {
                    ImGui::Text("ORM:");
                    ImGui::SameLine();
                    std::string displayPath = !layer.ormTextureRef.isValid() ? "(None)" :
                        std::filesystem::path(layer.ormTextureRef.resolve()).filename().string();
                    ImGui::TextDisabled("%s", displayPath.c_str());

                    ImGui::SameLine();
                    if (ImGui::SmallButton("Browse##orm"))
                    {
                        nfd::FileDialog fileDialog;
                        std::vector<std::pair<std::wstring, std::wstring>> filters = {
                            {L"VF Image", L"*.vfImage"}
                        };
                        std::string selectedPath = fileDialog.openFileDialog(filters);
                        if (!selectedPath.empty())
                        {
                            selectedPath.erase(
                                std::remove(selectedPath.begin(), selectedPath.end(), '\0'),
                                selectedPath.end());
                            layer.ormTextureRef = asset::AssetRef::fromPath(selectedPath);
                            onChanged();
                        }
                    }
                    if (layer.ormTextureRef.isValid())
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X##orm"))
                        {
                            layer.ormTextureRef = asset::AssetRef::invalid();
                            onChanged();
                        }
                    }
                }

                if (!layer.ormTextureRef.isValid())
                {
                    if (ImGui::DragFloat("Roughness", &layer.roughness, 0.01f, 0.0f, 1.0f))
                    {
                        onChanged();
                    }
                    if (ImGui::DragFloat("Metallic", &layer.metallic, 0.01f, 0.0f, 1.0f))
                    {
                        onChanged();
                    }
                    if (ImGui::DragFloat("AO", &layer.ao, 0.01f, 0.0f, 1.0f))
                    {
                        onChanged();
                    }
                }
                else
                {
                    ImGui::TextDisabled("PBR from ORM texture (R=AO, G=Rough, B=Metal)");
                }

                if (ImGui::DragFloat("Emission", &layer.emissionStrength, 0.01f, 0.0f, 10.0f))
                {
                    onChanged();
                }

                if (ImGui::DragFloat("Tiling", &layer.tilingScale, 0.01f, 0.01f, 100.0f))
                {
                    onChanged();
                }

                ImGui::Spacing();
                ImGui::BeginDisabled(layerCount <= 1);
                bool removeClicked = ImGui::SmallButton("Remove Layer");
                ImGui::EndDisabled();

                if (removeClicked)
                {
                    removeLayer(i, layerCount);
                    layerCount--;
                    onChanged();
                    ImGui::Unindent(8.0f);
                    ImGui::PopID();
                    break;
                }

                ImGui::Unindent(8.0f);
            }

            ImGui::PopID();
        }
    }
}
