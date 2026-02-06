#include "TerrainMaterialEditorWindow.hpp"
#include "../graph/ShaderGraphEditor.hpp"
#include "../graph/ShaderGraphCompiler.hpp"
#include "../graph/nodes/ShaderNode.hpp"
#include <terrain/TerrainMaterialAsset.hpp>
#include <resource/ResourceManager.hpp>
#include "imgui.h"
#include "print/EditorLogger.hpp"
#include <filesystem>
#include <fstream>

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

    void TerrainMaterialEditorWindow::compileMaterial()
    {
        if (!materialData) return;

        auto result = editor::graph::ShaderGraphCompiler::compileTerrainGraph(materialData->graph);

        if (result.success)
        {
            materialData->cachedMaterialSnippet = result.materialSnippet;
            materialData->needsRecompile = false;
            showCompileError = false;
            vfLogInfo("Terrain material compiled successfully: {}", materialData->name);

            // Write generated GLSL snippet to file
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
    }

    void TerrainMaterialEditorWindow::draw()
    {
        if (!isOpen) return;

        if (needsInit)
        {
            initEditor();
            needsInit = false;
        }

        ImGui::SetNextWindowSize(ImVec2(1000, 700), ImGuiCond_FirstUseEver);

        std::string title = windowTitle + (isDirty ? " *" : "  ");

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar;
        if (ImGui::Begin(title.c_str(), &isOpen, flags))
        {
            if (isOpen)
            {
                drawToolbar();
                drawGraphPanel();
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
}
