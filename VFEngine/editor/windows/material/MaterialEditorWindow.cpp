#include "MaterialEditorWindow.hpp"
#include "MaterialInstanceEditorWindow.hpp"
#include "MaterialPreviewPanel.hpp"
#include "OrmPackingDialog.hpp"
#include "MaterialPropertyPanel.hpp"
#include "../../graph/ShaderGraphEditor.hpp"
#include "../../graph/ShaderGraphCompiler.hpp"
#include "../../graph/nodes/ShaderNode.hpp"
#include <material/MaterialManager.hpp>
#include <material/MaterialInstanceTypes.hpp>
#include <material/MaterialRuntimeData.hpp>
#include <material/ToonProfileManager.hpp>
#include <resource/ResourceManager.hpp>
#include <resource/AssetTypes.hpp>
#include <asset/AssetRef.hpp>
#include "imgui.h"
#include "events/EventDispatcher.hpp"
#include "events/render/MaterialEvents.hpp"
#include "events/project/ResourceEvents.hpp"
#include <filesystem>

namespace windows
{
    MaterialEditorWindow::MaterialEditorWindow(const std::string& materialPath)
        : materialPath(materialPath)
          , graphEditor(std::make_unique<editor::graph::ShaderGraphEditor>())
          , previewPanel(std::make_unique<editor::materialeditor::MaterialPreviewPanel>(this))
          , ormPackDialog(std::make_unique<editor::materialeditor::OrmPackingDialog>())
          , propertyPanel(std::make_unique<editor::materialeditor::MaterialPropertyPanel>())
    {
        std::filesystem::path path(materialPath);
        windowTitle = "Material Editor: " + path.filename().string();

        propertyPanel->setOnPropertyChanged([this]() { onGraphChanged(); });
        propertyPanel->setOnParameterValueChanged([this]() { onParameterValueChanged(); });
        previewPanel->setOnBlendModeChanged([this]() { isDirty = true; });
    }

    MaterialEditorWindow::~MaterialEditorWindow()
    {
        if (graphEditor)
        {
            graphEditor->cleanUp();
        }
    }

    void MaterialEditorWindow::initEditor()
    {
        graphEditor->init();
        loadMaterial();

        if (materialData)
        {
            graphEditor->setGraph(&materialData->graph);
            graphEditor->setOnGraphChanged([this]() { onGraphChanged(); });
            graphEditor->navigateToContent();

            previewPanel->updateFromGraph(materialData, materialPath, false);
        }
    }

    void MaterialEditorWindow::loadMaterial()
    {
        materialData = resource::ResourceManager::loadMaterial(asset::AssetRef::fromPath(materialPath));

        if (!materialData)
        {
            vfLogInfo("Creating new material: {}", materialPath);
            std::filesystem::path path(materialPath);
            materialData = material::MaterialManager::instance().createMaterial(
                path.stem().string(), materialPath);
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

    void MaterialEditorWindow::saveMaterial()
    {
        if (!materialData) return;

        compileMaterial();

        if (material::MaterialManager::instance().saveMaterial(materialPath, *materialData))
        {
            isDirty = false;
            vfLogInfo("Material saved: {}", materialPath);

            events::material::MaterialFileSavedNotification notification;
            notification.materialPath = materialPath;
            events::EventDispatcher::instance().publish(notification);

            events::resource::AssetSavedNotification assetNotif;
            assetNotif.filePath = materialPath;
            events::EventDispatcher::instance().publish(assetNotif);

            // Note: updateFromGraph is already called in compileMaterial() above,
            // so we don't need to call it again here. Calling it twice can cause
            // descriptor set updates without proper GPU synchronization.
        }
        else
        {
            vfLogError("Failed to save material: {}", materialPath);
        }
    }

    void MaterialEditorWindow::compileMaterial()
    {
        if (!materialData) return;

        // VK-1493: when the material is Toon, refresh the resolved profile snapshot so the
        // baked TOON_* defines match the current profile, and enable the toon preview branch.
        editor::graph::ShaderCompileOptions opts;
        if (materialData->shadingModel == material::ShadingModel::Toon)
        {
            opts.toonEnabled = true;
            if (!materialData->toonProfile.empty())
            {
                if (auto profile = material::ToonProfileManager::instance().getOrLoad(materialData->toonProfile))
                    materialData->toonProfileValues = *profile;
            }
            opts.toonProfile = materialData->toonProfileValues;
        }

        auto result = editor::graph::ShaderGraphCompiler::compileGraph(materialData->graph, opts);

        if (result.success)
        {
            materialData->cachedVertexShader = result.vertexShader;
            materialData->cachedFragmentShader = result.fragmentShader;
            materialData->needsRecompile = false;
            // Refresh the in-memory IR hash / shader-map key so the render-side shader
            // cache (MaterialShaderCache::getOrCreatePipeline) doesn't treat this material
            // as perpetually stale — otherwise an IR-affecting edit such as switching to
            // Toon recompiles the shader every frame (VK-1493). Mirrors what save writes.
            {
                auto runtimeData = material::MaterialRuntimeDataBuilder::fromMaterialData(*materialData);
                materialData->irHash = runtimeData.irHash;
                materialData->shaderMapKey = runtimeData.shaderMap.shaderMapKey;
            }
            showCompileError = false;
            previewPanel->clearShaderError();
            vfLogInfo("Material compiled successfully: {}", materialData->name);

            previewPanel->updateFromGraph(materialData, materialPath, true);
        }
        else
        {
            showCompileError = true;
            compileErrorMessage = result.errorMessage;
            vfLogError("Material compilation failed: {}", result.errorMessage);
        }
    }

    void MaterialEditorWindow::onGraphChanged()
    {
        isDirty = true;
        materialData->needsRecompile = true;
    }

    void MaterialEditorWindow::onParameterValueChanged()
    {
        // Exposed parameter values live in the set-2 uniform block — the preview picks
        // them up through the shared MaterialData without recompiling the shader.
        isDirty = true;
        if (materialData)
        {
            bool customShaderActive = !materialData->cachedFragmentShader.empty() &&
                                      !materialData->needsRecompile;
            previewPanel->updateFromGraph(materialData, materialPath, customShaderActive);
        }
    }

    void MaterialEditorWindow::draw()
    {
        if (!isOpen)
        {
            previewPanel->cleanup();
            return;
        }

        if (needsInit)
        {
            initEditor();
            needsInit = false;
        }

        if (initialSize.x <= 0.0f)
        {
            initialSize = editor::preview::initialWindowSize("MaterialEditor", ImVec2(1200, 800));
        }
        ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
        maximizer.preBegin();

        std::string title = windowTitle + (isDirty ? " *" : "  ") +
                            "###MaterialEditor:" + materialPath;

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar;
        flags |= maximizer.windowFlags();
        if (ImGui::Begin(title.c_str(), &isOpen, flags))
        {
            if (isOpen)
            {
                drawToolbar();

                ImVec2 contentSize = ImGui::GetContentRegionAvail();
                const float splitterThickness = 5.0f;
                float spacingY = ImGui::GetStyle().ItemSpacing.y;

                graphHeightFraction = std::clamp(graphHeightFraction, 0.3f, 0.9f);
                float graphHeight = contentSize.y * graphHeightFraction;
                float bottomHeight = contentSize.y - graphHeight - splitterThickness - spacingY * 2.0f;

                previewPanelWidth = std::clamp(previewPanelWidth, 180.0f,
                                               std::max(180.0f, contentSize.x - 300.0f - splitterThickness));

                ImGui::BeginChild("TopRow", ImVec2(0, graphHeight), false, ImGuiWindowFlags_NoScrollbar);
                {
                    ImVec2 topSize = ImGui::GetContentRegionAvail();

                    ImGui::BeginChild("PreviewPanel", ImVec2(previewPanelWidth, topSize.y), true);
                    previewPanel->draw(materialData, materialPath);
                    ImGui::EndChild();

                    ImGui::SameLine(0.0f, 0.0f);
                    float graphWidth = topSize.x - previewPanelWidth - splitterThickness;
                    editor::preview::splitterV("##matSplit", splitterThickness, &previewPanelWidth,
                                               &graphWidth, 180.0f, 300.0f, topSize.y);
                    ImGui::SameLine(0.0f, 0.0f);

                    ImGui::BeginChild("GraphPanel", ImVec2(graphWidth, topSize.y), true,
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    drawGraphPanel();
                    ImGui::EndChild();
                }
                ImGui::EndChild();

                if (editor::preview::splitterH("##matRowSplit", splitterThickness, &graphHeight,
                                               &bottomHeight, 150.0f, 100.0f, contentSize.x))
                {
                    graphHeightFraction = graphHeight / std::max(contentSize.y, 1.0f);
                }

                ImGui::BeginChild("BottomRow", ImVec2(0, bottomHeight), false, ImGuiWindowFlags_NoScrollbar);
                {
                    ImVec2 bottomSize = ImGui::GetContentRegionAvail();

                    ImGui::BeginChild("ParametersPanel", ImVec2(previewPanelWidth, bottomSize.y), true);
                    propertyPanel->drawParameterPanel(materialData);
                    ImGui::EndChild();

                    ImGui::SameLine();

                    float propsWidth = bottomSize.x - previewPanelWidth - ImGui::GetStyle().ItemSpacing.x;
                    ImGui::BeginChild("PropertiesPanel", ImVec2(propsWidth, bottomSize.y), true);
                    propertyPanel->drawPropertiesPanel(materialData, graphEditor.get());
                    ImGui::EndChild();
                }
                ImGui::EndChild();
            }

            ormPackDialog->draw();
        }
        ImGui::End();

        if (!isOpen && !sizeSaved)
        {
            editor::preview::rememberWindowSize("MaterialEditor", maximizer.effectiveSize());
            sizeSaved = true;
        }

        if (previewPanel->hasShaderError() && !showCompileError)
        {
            showCompileError = true;
            compileErrorMessage = previewPanel->getShaderError();
        }
    }

    void MaterialEditorWindow::drawToolbar()
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

            if (ImGui::BeginMenu("Tools"))
            {
                if (ImGui::MenuItem("Pack ORM Texture..."))
                {
                    ormPackDialog->open();
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
        if (ImGui::Button("Create Instance"))
        {
            std::string instancePath = material::getDefaultInstancePath(materialPath);

            auto& manager = material::MaterialManager::instance();
            std::string instanceName = materialData ? materialData->name + " Instance" : "New Instance";
            auto instance = manager.createInstance(instanceName, materialPath, instancePath);

            if (instance)
            {
                events::resource::ImportCompletedNotification notification;
                services::ImportResult res;
                res.sourcePath = instancePath;
                res.outputPath = instancePath;
                res.assetType = resource::AssetType::MaterialInstance;
                res.success = true;
                notification.results.push_back(res);
                events::EventDispatcher::instance().publish(notification);
            }
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Create a new material instance based on this material");
            ImGui::EndTooltip();
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

        ImGui::SameLine();
        maximizer.drawButton();

        ImGui::Separator();
    }

    void MaterialEditorWindow::drawGraphPanel()
    {
        if (graphEditor && materialData)
        {
            graphEditor->draw();
        }
        else
        {
            ImGui::TextDisabled("No material loaded");
        }
    }
}
