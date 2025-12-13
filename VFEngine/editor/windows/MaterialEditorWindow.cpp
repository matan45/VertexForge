#include "MaterialEditorWindow.hpp"
#include "../graph/ShaderGraphEditor.hpp"
#include "../graph/ShaderGraphCompiler.hpp"
#include <material/MaterialManager.hpp>
#include <material/MaterialAsset.hpp>
#include "imgui.h"
#include "print/EditorLogger.hpp"
#include <filesystem>

namespace windows {

    MaterialEditorWindow::MaterialEditorWindow(const std::string& materialPath)
        : materialPath(materialPath)
        , graphEditor(std::make_unique<editor::graph::ShaderGraphEditor>())
    {
        std::filesystem::path path(materialPath);
        windowTitle = "Material Editor: " + path.filename().string();
    }

    MaterialEditorWindow::~MaterialEditorWindow() {
        if (graphEditor) {
            graphEditor->cleanUp();
        }
    }

    void MaterialEditorWindow::initEditor() {
        graphEditor->init();
        loadMaterial();

        if (materialData) {
            graphEditor->setGraph(&materialData->graph);
            graphEditor->setOnGraphChanged([this]() { onGraphChanged(); });
            graphEditor->navigateToContent();
        }
    }

    void MaterialEditorWindow::loadMaterial() {
        // Try to load from MaterialManager cache first
        materialData = material::MaterialManager::instance().loadMaterial(materialPath);

        if (!materialData) {
            // Create a new material if it doesn't exist
            vfLogInfo("Creating new material: {}", materialPath);
            std::filesystem::path path(materialPath);
            materialData = material::MaterialManager::instance().createMaterial(
                path.stem().string(), materialPath);
        }
    }

    void MaterialEditorWindow::saveMaterial() {
        if (!materialData) return;

        // Compile first to update cached shaders
        compileMaterial();

        if (material::MaterialManager::instance().saveMaterial(materialPath, *materialData)) {
            isDirty = false;
            vfLogInfo("Material saved: {}", materialPath);
        } else {
            vfLogError("Failed to save material: {}", materialPath);
        }
    }

    void MaterialEditorWindow::compileMaterial() {
        if (!materialData) return;

        auto result = editor::graph::ShaderGraphCompiler::compileGraph(materialData->graph);

        if (result.success) {
            materialData->cachedVertexShader = result.vertexShader;
            materialData->cachedFragmentShader = result.fragmentShader;
            materialData->needsRecompile = false;
            showCompileError = false;
            vfLogInfo("Material compiled successfully: {}", materialData->name);
        } else {
            showCompileError = true;
            compileErrorMessage = result.errorMessage;
            vfLogError("Material compilation failed: {}", result.errorMessage);
        }
    }

    void MaterialEditorWindow::onGraphChanged() {
        isDirty = true;
        materialData->needsRecompile = true;
    }

    void MaterialEditorWindow::draw() {
        if (!isOpen) return;

        if (needsInit) {
            initEditor();
            needsInit = false;
        }

        ImGui::SetNextWindowSize(ImVec2(1200, 800), ImGuiCond_FirstUseEver);

        std::string title = windowTitle;
        if (isDirty) title += " *";

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar;
        if (ImGui::Begin(title.c_str(), &isOpen, flags)) {
            if (isOpen) {
                drawToolbar();

                ImVec2 contentSize = ImGui::GetContentRegionAvail();

                // Layout: [Preview Panel] [Graph Editor]
                //         [Parameters   ] [Properties  ]

                float graphHeight = contentSize.y * 0.7f;
                float bottomHeight = contentSize.y - graphHeight - ImGui::GetStyle().ItemSpacing.y;

                // Top row
                ImGui::BeginChild("TopRow", ImVec2(0, graphHeight), false);
                {
                    ImVec2 topSize = ImGui::GetContentRegionAvail();

                    // Preview panel (left)
                    ImGui::BeginChild("PreviewPanel", ImVec2(previewPanelWidth, topSize.y), true);
                    drawPreviewPanel(topSize.y);
                    ImGui::EndChild();

                    ImGui::SameLine();

                    // Graph panel (right)
                    float graphWidth = topSize.x - previewPanelWidth - ImGui::GetStyle().ItemSpacing.x;
                    ImGui::BeginChild("GraphPanel", ImVec2(graphWidth, topSize.y), true,
                                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    drawGraphPanel(graphWidth, topSize.y);
                    ImGui::EndChild();
                }
                ImGui::EndChild();

                // Bottom row
                ImGui::BeginChild("BottomRow", ImVec2(0, bottomHeight), false);
                {
                    ImVec2 bottomSize = ImGui::GetContentRegionAvail();

                    // Parameters panel (left)
                    ImGui::BeginChild("ParametersPanel", ImVec2(previewPanelWidth, bottomSize.y), true);
                    drawParameterPanel();
                    ImGui::EndChild();

                    ImGui::SameLine();

                    // Properties panel (right)
                    float propsWidth = bottomSize.x - previewPanelWidth - ImGui::GetStyle().ItemSpacing.x;
                    ImGui::BeginChild("PropertiesPanel", ImVec2(propsWidth, bottomSize.y), true);
                    drawPropertiesPanel();
                    ImGui::EndChild();
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();
    }

    void MaterialEditorWindow::drawToolbar() {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Save", "Ctrl+S")) {
                    saveMaterial();
                }
                if (ImGui::MenuItem("Compile", "F5")) {
                    compileMaterial();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Close")) {
                    isOpen = false;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Navigate to Content")) {
                    graphEditor->navigateToContent();
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        // Toolbar buttons
        if (ImGui::Button("Save")) {
            saveMaterial();
        }
        ImGui::SameLine();
        if (ImGui::Button("Compile")) {
            compileMaterial();
        }

        // Compile status
        ImGui::SameLine();
        if (showCompileError) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Compile Error!");
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(compileErrorMessage.c_str());
                ImGui::EndTooltip();
            }
        } else if (materialData && !materialData->needsRecompile) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Compiled");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Needs Compile");
        }

        ImGui::Separator();
    }

    void MaterialEditorWindow::drawPreviewPanel(float height) {
        ImGui::Text("Preview");
        ImGui::Separator();

        // Placeholder for sphere preview
        ImVec2 previewSize = ImGui::GetContentRegionAvail();
        previewSize.y = previewSize.x;  // Square aspect ratio

        ImGui::BeginChild("PreviewViewport", previewSize, true,
                         ImGuiWindowFlags_NoScrollbar);
        {
            // Draw a placeholder rectangle
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(pos, ImVec2(pos.x + previewSize.x - 20, pos.y + previewSize.y - 20),
                                   IM_COL32(40, 40, 40, 255));

            // Draw a simple sphere representation
            ImVec2 center(pos.x + previewSize.x / 2 - 10, pos.y + previewSize.y / 2 - 10);
            float radius = std::min(previewSize.x, previewSize.y) / 3;
            drawList->AddCircleFilled(center, radius, IM_COL32(100, 100, 100, 255));
            drawList->AddCircle(center, radius, IM_COL32(150, 150, 150, 255), 32, 2.0f);

            // Add highlight
            ImVec2 highlight(center.x - radius * 0.3f, center.y - radius * 0.3f);
            drawList->AddCircleFilled(highlight, radius * 0.15f, IM_COL32(200, 200, 200, 100));

            ImGui::TextDisabled("(Preview coming soon)");
        }
        ImGui::EndChild();

        ImGui::Spacing();

        // Quick material settings
        if (materialData) {
            ImGui::Text("Blend Mode");
            const char* blendModes[] = { "Opaque", "Masked", "Translucent" };
            int blendMode = static_cast<int>(materialData->blendMode);
            if (ImGui::Combo("##BlendMode", &blendMode, blendModes, 3)) {
                materialData->blendMode = static_cast<material::BlendMode>(blendMode);
                isDirty = true;
            }

            if (ImGui::Checkbox("Two Sided", &materialData->twoSided)) {
                isDirty = true;
            }
        }
    }

    void MaterialEditorWindow::drawGraphPanel(float width, float height) {
        if (graphEditor && materialData) {
            graphEditor->draw();
        } else {
            ImGui::TextDisabled("No material loaded");
        }
    }

    void MaterialEditorWindow::drawParameterPanel() {
        ImGui::Text("Parameters");
        ImGui::Separator();

        if (!materialData) {
            ImGui::TextDisabled("No material");
            return;
        }

        // Display and edit material parameters
        for (auto& [name, param] : materialData->parameters) {
            ImGui::PushID(name.c_str());

            switch (param.type) {
                case material::ParameterType::Scalar: {
                    if (std::holds_alternative<float>(param.value)) {
                        float value = std::get<float>(param.value);
                        if (ImGui::SliderFloat(name.c_str(), &value, param.min, param.max)) {
                            param.value = value;
                            isDirty = true;
                        }
                    }
                    break;
                }
                case material::ParameterType::Vec2: {
                    if (std::holds_alternative<glm::vec2>(param.value)) {
                        glm::vec2 value = std::get<glm::vec2>(param.value);
                        if (ImGui::DragFloat2(name.c_str(), &value.x, 0.01f)) {
                            param.value = value;
                            isDirty = true;
                        }
                    }
                    break;
                }
                case material::ParameterType::Vec3: {
                    if (std::holds_alternative<glm::vec4>(param.value)) {
                        glm::vec4 value = std::get<glm::vec4>(param.value);
                        glm::vec3 v3(value);
                        if (ImGui::DragFloat3(name.c_str(), &v3.x, 0.01f)) {
                            param.value = glm::vec4(v3, value.w);
                            isDirty = true;
                        }
                    }
                    break;
                }
                case material::ParameterType::Vec4: {
                    if (std::holds_alternative<glm::vec4>(param.value)) {
                        glm::vec4 value = std::get<glm::vec4>(param.value);
                        if (ImGui::DragFloat4(name.c_str(), &value.x, 0.01f)) {
                            param.value = value;
                            isDirty = true;
                        }
                    }
                    break;
                }
                case material::ParameterType::Color: {
                    if (std::holds_alternative<glm::vec4>(param.value)) {
                        glm::vec4 value = std::get<glm::vec4>(param.value);
                        if (ImGui::ColorEdit4(name.c_str(), &value.x)) {
                            param.value = value;
                            isDirty = true;
                        }
                    }
                    break;
                }
            }

            ImGui::PopID();
        }

        if (materialData->parameters.empty()) {
            ImGui::TextDisabled("No exposed parameters");
        }
    }

    void MaterialEditorWindow::drawPropertiesPanel() {
        ImGui::Text("Node Properties");
        ImGui::Separator();

        if (!graphEditor || !materialData) {
            ImGui::TextDisabled("No material");
            return;
        }

        uint32_t selectedId = graphEditor->getSelectedNodeId();
        if (selectedId == 0) {
            ImGui::TextDisabled("Select a node to edit properties");
            return;
        }

        // Find selected node
        material::ShaderNode* selectedNode = materialData->graph.findNode(selectedId);
        if (!selectedNode) {
            ImGui::TextDisabled("Node not found");
            return;
        }

        ImGui::Text("Node: %s", selectedNode->name.c_str());
        ImGui::Separator();

        // Edit node properties based on type
        bool changed = false;

        for (auto& [propName, propValue] : selectedNode->properties) {
            ImGui::PushID(propName.c_str());

            if (std::holds_alternative<float>(propValue)) {
                float value = std::get<float>(propValue);
                if (ImGui::DragFloat(propName.c_str(), &value, 0.01f, 0.0f, 1.0f)) {
                    propValue = value;
                    changed = true;
                }
            }
            else if (std::holds_alternative<glm::vec2>(propValue)) {
                glm::vec2 value = std::get<glm::vec2>(propValue);
                if (ImGui::DragFloat2(propName.c_str(), &value.x, 0.01f)) {
                    propValue = value;
                    changed = true;
                }
            }
            else if (std::holds_alternative<glm::vec3>(propValue)) {
                glm::vec3 value = std::get<glm::vec3>(propValue);
                // Check if this is a color node
                if (selectedNode->type == material::NodeType::ConstantColor) {
                    if (ImGui::ColorEdit3(propName.c_str(), &value.x)) {
                        propValue = value;
                        changed = true;
                    }
                } else {
                    if (ImGui::DragFloat3(propName.c_str(), &value.x, 0.01f)) {
                        propValue = value;
                        changed = true;
                    }
                }
            }
            else if (std::holds_alternative<glm::vec4>(propValue)) {
                glm::vec4 value = std::get<glm::vec4>(propValue);
                if (ImGui::ColorEdit4(propName.c_str(), &value.x)) {
                    propValue = value;
                    changed = true;
                }
            }
            else if (std::holds_alternative<std::string>(propValue)) {
                std::string value = std::get<std::string>(propValue);
                char buffer[256];
                strncpy_s(buffer, value.c_str(), sizeof(buffer) - 1);
                if (ImGui::InputText(propName.c_str(), buffer, sizeof(buffer))) {
                    propValue = std::string(buffer);
                    changed = true;
                }
            }

            ImGui::PopID();
        }

        if (changed) {
            onGraphChanged();
        }

        if (selectedNode->properties.empty()) {
            ImGui::TextDisabled("No editable properties");
        }
    }

}
