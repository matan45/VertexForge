#include "TerrainMaterialEditorWindow.hpp"
#include "../graph/ShaderGraphEditor.hpp"
#include "../graph/ShaderGraphCompiler.hpp"
#include "../graph/nodes/ShaderNode.hpp"
#include <terrain/TerrainMaterialAsset.hpp>
#include <resource/ResourceManager.hpp>
#include "nfd/FileDialog.hpp"
#include "imgui.h"
#include "print/EditorLogger.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <format>

namespace windows
{
    TerrainMaterialEditorWindow::TerrainMaterialEditorWindow(const std::string& materialPath)
        : materialPath(materialPath)
          , graphEditor(std::make_unique<editor::graph::ShaderGraphEditor>())
    {
        std::filesystem::path path(materialPath);
        windowTitle = "Terrain Material: " + path.filename().string();
    }

    TerrainMaterialEditorWindow::~TerrainMaterialEditorWindow()
    {
        if (graphEditor)
        {
            graphEditor->cleanUp();
        }
    }

    void TerrainMaterialEditorWindow::initEditor()
    {
        graphEditor->init();
        loadMaterial();

        if (materialData)
        {
            graphEditor->setGraph(&materialData->graph);
            graphEditor->setOnGraphChanged([this]() { onGraphChanged(); });
            graphEditor->navigateToContent();
        }
    }

    void TerrainMaterialEditorWindow::loadMaterial()
    {
        materialData = resource::ResourceManager::loadTerrainMaterial(materialPath);

        if (!materialData)
        {
            vfLogInfo("Creating new terrain material: {}", materialPath);
            std::filesystem::path path(materialPath);
            auto newMaterial = terrain::TerrainMaterialAsset::createDefault(path.stem().string());
            materialData = std::make_shared<terrain::TerrainMaterialData>(std::move(newMaterial));
        }

        if (materialData)
        {
            for (auto& node : materialData->graph.nodes)
            {
                if (node.inputs.empty() && node.outputs.empty())
                {
                    editor::graph::ShaderNodeFactory::initializeNode(node, materialData->graph.nextPinId);
                }
            }
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
        }
        else
        {
            vfLogError("Failed to save terrain material: {}", materialPath);
        }
    }

    void TerrainMaterialEditorWindow::syncLayersFromGraph()
    {
        for (const auto& node : materialData->graph.nodes)
        {
            if (node.type != material::NodeType::TerrainLayerStack) continue;

            int layerCount = 1;
            auto lcIt = node.properties.find("layerCount");
            if (lcIt != node.properties.end())
            {
                if (auto* f = std::get_if<float>(&lcIt->second))
                    layerCount = std::clamp(static_cast<int>(*f), 1, static_cast<int>(terrain::MAX_TERRAIN_LAYERS));
            }
            materialData->activeLayerCount = static_cast<uint8_t>(layerCount);

            for (int i = 0; i < layerCount; ++i)
            {
                std::string prefix = "layer" + std::to_string(i) + "_";
                auto& layer = materialData->layers[i];

                auto nameIt = node.properties.find(prefix + "name");
                if (nameIt != node.properties.end())
                {
                    if (auto* s = std::get_if<std::string>(&nameIt->second))
                        layer.name = *s;
                }

                auto albedoIt = node.properties.find(prefix + "albedo");
                if (albedoIt != node.properties.end())
                {
                    if (auto* s = std::get_if<std::string>(&albedoIt->second))
                        layer.albedoTexturePath = *s;
                }

                auto normalIt = node.properties.find(prefix + "normal");
                if (normalIt != node.properties.end())
                {
                    if (auto* s = std::get_if<std::string>(&normalIt->second))
                        layer.normalTexturePath = *s;
                }

                auto tilingIt = node.properties.find(prefix + "tiling");
                if (tilingIt != node.properties.end())
                {
                    if (auto* f = std::get_if<float>(&tilingIt->second))
                        layer.tilingScale = *f;
                }

                auto blendIt = node.properties.find(prefix + "blendMode");
                if (blendIt != node.properties.end())
                {
                    if (auto* s = std::get_if<std::string>(&blendIt->second))
                        layer.blendMode = terrain::stringToLayerBlendMode(*s);
                }

                auto enabledIt = node.properties.find(prefix + "enabled");
                if (enabledIt != node.properties.end())
                {
                    if (auto* f = std::get_if<float>(&enabledIt->second))
                        layer.enabled = (*f > 0.5f);
                }
            }
            break;
        }
    }

    void TerrainMaterialEditorWindow::syncLayersToGraph()
    {
        for (auto& node : materialData->graph.nodes)
        {
            if (node.type != material::NodeType::TerrainLayerStack) continue;

            int layerCount = materialData->activeLayerCount;
            node.properties["layerCount"] = static_cast<float>(layerCount);

            for (int i = 0; i < layerCount; ++i)
            {
                std::string prefix = "layer" + std::to_string(i) + "_";
                const auto& layer = materialData->layers[i];

                node.properties[prefix + "name"] = layer.name;
                node.properties[prefix + "albedo"] = layer.albedoTexturePath;
                node.properties[prefix + "normal"] = layer.normalTexturePath;
                node.properties[prefix + "tiling"] = layer.tilingScale;
                node.properties[prefix + "blendMode"] = terrain::blendModeToString(layer.blendMode);
                node.properties[prefix + "enabled"] = layer.enabled ? 1.0f : 0.0f;
            }

            // Clean up properties beyond active count
            for (int i = layerCount; i < terrain::MAX_TERRAIN_LAYERS; ++i)
            {
                std::string prefix = "layer" + std::to_string(i) + "_";
                const std::vector<std::string> suffixes = {"name", "albedo", "normal", "tiling", "blendMode", "enabled"};
                for (const auto& suffix : suffixes)
                {
                    node.properties.erase(prefix + suffix);
                }
            }
            break;
        }
    }

    void TerrainMaterialEditorWindow::removeLayer(material::ShaderNode& node, int removeIndex, int currentCount)
    {
        const std::vector<std::string> suffixes = {"name", "albedo", "normal", "tiling", "blendMode", "enabled"};

        // Shift layers down in material data
        for (int i = removeIndex; i < currentCount - 1; ++i)
        {
            materialData->layers[i] = materialData->layers[i + 1];
        }
        materialData->layers[currentCount - 1] = terrain::TerrainMaterialLayer{};

        // Shift node properties down
        for (int i = removeIndex; i < currentCount - 1; ++i)
        {
            std::string srcPrefix = "layer" + std::to_string(i + 1) + "_";
            std::string dstPrefix = "layer" + std::to_string(i) + "_";

            for (const auto& suffix : suffixes)
            {
                auto srcIt = node.properties.find(srcPrefix + suffix);
                if (srcIt != node.properties.end())
                    node.properties[dstPrefix + suffix] = srcIt->second;
            }
        }

        // Remove trailing layer properties
        std::string lastPrefix = "layer" + std::to_string(currentCount - 1) + "_";
        for (const auto& suffix : suffixes)
        {
            node.properties.erase(lastPrefix + suffix);
        }

        // Update count
        int newCount = currentCount - 1;
        materialData->activeLayerCount = static_cast<uint8_t>(newCount);
        node.properties["layerCount"] = static_cast<float>(newCount);
    }

    void TerrainMaterialEditorWindow::compileMaterial()
    {
        if (!materialData) return;

        syncLayersFromGraph();

        auto result = editor::graph::ShaderGraphCompiler::compileTerrainGraph(materialData->graph);

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
        }
        else
        {
            showCompileError = true;
            compileErrorMessage = result.errorMessage;
            vfLogError("Terrain material compilation failed: {}", result.errorMessage);
        }
    }

    void TerrainMaterialEditorWindow::onGraphChanged()
    {
        isDirty = true;
        materialData->needsRecompile = true;
        autoCompileCountdown = AUTO_COMPILE_DELAY_FRAMES;
    }

    void TerrainMaterialEditorWindow::draw()
    {
        if (!isOpen) return;

        if (needsInit)
        {
            initEditor();
            needsInit = false;
        }

        // Debounced auto-compile for immediate feedback
        if (autoCompileCountdown > 0)
        {
            autoCompileCountdown--;
            if (autoCompileCountdown == 0 && materialData && materialData->needsRecompile)
            {
                compileMaterial();
            }
        }

        ImGui::SetNextWindowSize(ImVec2(1200, 800), ImGuiCond_FirstUseEver);

        std::string title = windowTitle + (isDirty ? " *" : "  ");

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar;
        if (ImGui::Begin(title.c_str(), &isOpen, flags))
        {
            if (isOpen)
            {
                drawToolbar();

                ImVec2 contentSize = ImGui::GetContentRegionAvail();
                float graphHeight = contentSize.y * 0.65f;
                float propsHeight = contentSize.y - graphHeight - ImGui::GetStyle().ItemSpacing.y;

                // Graph panel (top)
                ImGui::BeginChild("GraphPanel", ImVec2(0, graphHeight), true,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                drawGraphPanel();
                ImGui::EndChild();

                // Properties panel (bottom)
                ImGui::BeginChild("PropertiesPanel", ImVec2(0, propsHeight), true);
                drawPropertiesPanel();
                ImGui::EndChild();
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

            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Navigate to Content"))
                {
                    graphEditor->navigateToContent();
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
        ImGui::BeginGroup();
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
        ImGui::EndGroup();

        ImGui::Separator();
    }

    void TerrainMaterialEditorWindow::drawGraphPanel()
    {
        if (graphEditor && materialData)
        {
            graphEditor->draw();
        }
        else
        {
            ImGui::TextDisabled("No terrain material loaded");
        }
    }

    void TerrainMaterialEditorWindow::drawPropertiesPanel()
    {
        if (!graphEditor || !materialData)
        {
            ImGui::TextDisabled("No material");
            return;
        }

        uint32_t selectedId = graphEditor->getSelectedNodeId();
        if (selectedId == 0)
        {
            ImGui::TextDisabled("Select a node to edit properties");
            return;
        }

        material::ShaderNode* selectedNode = materialData->graph.findNode(selectedId);
        if (!selectedNode)
        {
            ImGui::TextDisabled("Node not found");
            return;
        }

        ImGui::Text("Node: %s", selectedNode->name.c_str());
        ImGui::Separator();

        bool changed = false;

        // Layer Stack node - custom UI with per-layer texture pickers
        if (selectedNode->type == material::NodeType::TerrainLayerStack)
        {
            changed |= drawLayerStackProperties(*selectedNode);
        }
        else
        {
            // Generic property editing for other nodes
            changed |= drawGenericProperties(*selectedNode);
        }

        if (changed)
        {
            onGraphChanged();
        }
    }

    bool TerrainMaterialEditorWindow::drawLayerStackProperties(material::ShaderNode& node)
    {
        bool changed = false;

        // Read layer count
        int layerCount = 1;
        auto lcIt = node.properties.find("layerCount");
        if (lcIt != node.properties.end())
        {
            if (auto* f = std::get_if<float>(&lcIt->second))
                layerCount = static_cast<int>(*f);
        }

        // Layer count display + Add button
        ImGui::Text("Layers: %d / %d", layerCount, terrain::MAX_TERRAIN_LAYERS);
        ImGui::SameLine();
        if (layerCount < terrain::MAX_TERRAIN_LAYERS)
        {
            if (ImGui::SmallButton("+ Add Layer"))
            {
                auto& newLayer = materialData->layers[layerCount];
                newLayer = terrain::TerrainMaterialLayer{};
                newLayer.name = "Layer " + std::to_string(layerCount);

                std::string prefix = "layer" + std::to_string(layerCount) + "_";
                node.properties[prefix + "name"] = newLayer.name;
                node.properties[prefix + "albedo"] = std::string("");
                node.properties[prefix + "normal"] = std::string("");
                node.properties[prefix + "tiling"] = 1.0f;
                node.properties[prefix + "blendMode"] = std::string("Linear");
                node.properties[prefix + "enabled"] = 1.0f;

                layerCount++;
                materialData->activeLayerCount = static_cast<uint8_t>(layerCount);
                node.properties["layerCount"] = static_cast<float>(layerCount);
                changed = true;
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

        // Per-layer definitions
        for (int i = 0; i < layerCount; ++i)
        {
            std::string prefix = "layer" + std::to_string(i) + "_";

            // Ensure properties exist
            if (node.properties.find(prefix + "name") == node.properties.end())
                node.properties[prefix + "name"] = std::string("Layer " + std::to_string(i));
            if (node.properties.find(prefix + "albedo") == node.properties.end())
                node.properties[prefix + "albedo"] = std::string("");
            if (node.properties.find(prefix + "normal") == node.properties.end())
                node.properties[prefix + "normal"] = std::string("");
            if (node.properties.find(prefix + "tiling") == node.properties.end())
                node.properties[prefix + "tiling"] = 1.0f;
            if (node.properties.find(prefix + "blendMode") == node.properties.end())
                node.properties[prefix + "blendMode"] = std::string("Linear");
            if (node.properties.find(prefix + "enabled") == node.properties.end())
                node.properties[prefix + "enabled"] = 1.0f;

            ImGui::PushID(i);

            // Enabled checkbox
            bool layerEnabled = true;
            if (auto* f = std::get_if<float>(&node.properties[prefix + "enabled"]))
                layerEnabled = (*f > 0.5f);
            if (ImGui::Checkbox("##enabled", &layerEnabled))
            {
                node.properties[prefix + "enabled"] = layerEnabled ? 1.0f : 0.0f;
                changed = true;
            }
            ImGui::SameLine();

            // Layer header with name
            std::string layerName = "Layer " + std::to_string(i);
            if (auto* s = std::get_if<std::string>(&node.properties[prefix + "name"]))
                layerName = *s;

            std::string headerLabel = std::format("{} ({})", layerName, i);
            if (ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent(8.0f);

                // Layer name
                {
                    char nameBuffer[64];
                    strncpy_s(nameBuffer, sizeof(nameBuffer), layerName.c_str(), sizeof(nameBuffer) - 1);
                    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
                    {
                        node.properties[prefix + "name"] = std::string(nameBuffer);
                        changed = true;
                    }
                }

                // Blend mode (not shown for layer 0 - it's the base)
                if (i > 0)
                {
                    std::string blendStr = "Linear";
                    if (auto* s = std::get_if<std::string>(&node.properties[prefix + "blendMode"]))
                        blendStr = *s;

                    const char* blendModes[] = {"Linear", "HeightBased", "Overlay"};
                    int currentBlend = 0;
                    if (blendStr == "HeightBased") currentBlend = 1;
                    else if (blendStr == "Overlay") currentBlend = 2;

                    if (ImGui::Combo("Blend Mode", &currentBlend, blendModes, IM_ARRAYSIZE(blendModes)))
                    {
                        node.properties[prefix + "blendMode"] = std::string(blendModes[currentBlend]);
                        changed = true;
                    }
                }

                // Albedo texture
                {
                    std::string albedoPath;
                    if (auto* s = std::get_if<std::string>(&node.properties[prefix + "albedo"]))
                        albedoPath = *s;

                    ImGui::Text("Albedo:");
                    ImGui::SameLine();
                    std::string displayPath = albedoPath.empty() ? "(None)" :
                        std::filesystem::path(albedoPath).filename().string();
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
                            node.properties[prefix + "albedo"] = selectedPath;
                            changed = true;
                        }
                    }
                    if (!albedoPath.empty())
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X##albedo"))
                        {
                            node.properties[prefix + "albedo"] = std::string("");
                            changed = true;
                        }
                    }
                }

                // Normal texture
                {
                    std::string normalPath;
                    if (auto* s = std::get_if<std::string>(&node.properties[prefix + "normal"]))
                        normalPath = *s;

                    ImGui::Text("Normal:");
                    ImGui::SameLine();
                    std::string displayPath = normalPath.empty() ? "(None)" :
                        std::filesystem::path(normalPath).filename().string();
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
                            node.properties[prefix + "normal"] = selectedPath;
                            changed = true;
                        }
                    }
                    if (!normalPath.empty())
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X##normal"))
                        {
                            node.properties[prefix + "normal"] = std::string("");
                            changed = true;
                        }
                    }
                }

                // Tiling scale
                {
                    float tiling = 1.0f;
                    if (auto* f = std::get_if<float>(&node.properties[prefix + "tiling"]))
                        tiling = *f;

                    if (ImGui::DragFloat("Tiling", &tiling, 0.01f, 0.01f, 100.0f))
                    {
                        node.properties[prefix + "tiling"] = tiling;
                        changed = true;
                    }
                }

                // Remove layer button
                ImGui::Spacing();
                ImGui::BeginDisabled(layerCount <= 1);
                if (ImGui::SmallButton("Remove Layer"))
                {
                    removeLayer(node, i, layerCount);
                    layerCount--;
                    changed = true;
                    ImGui::EndDisabled();
                    ImGui::Unindent(8.0f);
                    ImGui::PopID();
                    break;
                }
                ImGui::EndDisabled();

                ImGui::Unindent(8.0f);
            }

            ImGui::PopID();
        }

        return changed;
    }

    bool TerrainMaterialEditorWindow::drawGenericProperties(material::ShaderNode& node)
    {
        bool changed = false;

        for (auto& [propName, propValue] : node.properties)
        {
            ImGui::PushID(propName.c_str());

            if (std::holds_alternative<float>(propValue))
            {
                float value = std::get<float>(propValue);
                if (ImGui::DragFloat(propName.c_str(), &value, 0.01f))
                {
                    propValue = value;
                    changed = true;
                }
            }
            else if (std::holds_alternative<glm::vec2>(propValue))
            {
                glm::vec2 value = std::get<glm::vec2>(propValue);
                if (ImGui::DragFloat2(propName.c_str(), &value.x, 0.01f))
                {
                    propValue = value;
                    changed = true;
                }
            }
            else if (std::holds_alternative<glm::vec3>(propValue))
            {
                glm::vec3 value = std::get<glm::vec3>(propValue);
                if (node.type == material::NodeType::ConstantColor)
                {
                    if (ImGui::ColorEdit3(propName.c_str(), &value.x))
                    {
                        propValue = value;
                        changed = true;
                    }
                }
                else
                {
                    if (ImGui::DragFloat3(propName.c_str(), &value.x, 0.01f))
                    {
                        propValue = value;
                        changed = true;
                    }
                }
            }
            else if (std::holds_alternative<glm::vec4>(propValue))
            {
                glm::vec4 value = std::get<glm::vec4>(propValue);
                if (ImGui::ColorEdit4(propName.c_str(), &value.x))
                {
                    propValue = value;
                    changed = true;
                }
            }
            else if (std::holds_alternative<std::string>(propValue))
            {
                std::string value = std::get<std::string>(propValue);
                char buffer[256];
                strncpy_s(buffer, sizeof(buffer), value.c_str(), sizeof(buffer) - 1);
                if (ImGui::InputText(propName.c_str(), buffer, sizeof(buffer)))
                {
                    propValue = std::string(buffer);
                    changed = true;
                }
            }

            ImGui::PopID();
        }

        if (node.properties.empty())
        {
            ImGui::TextDisabled("No editable properties");
        }

        return changed;
    }
}
