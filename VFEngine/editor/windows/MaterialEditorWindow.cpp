#include "MaterialEditorWindow.hpp"
#include "../graph/ShaderGraphEditor.hpp"
#include "../graph/ShaderGraphCompiler.hpp"
#include "../graph/nodes/ShaderNode.hpp"
#include "../camera/OrbitCamera.hpp"
#include "../../graphics/controllers/MaterialPreviewController.hpp"
#include <material/MaterialManager.hpp>
#include <material/MaterialAsset.hpp>
#include <nfd/FileDialog.hpp>
#include "imgui.h"
#include "print/EditorLogger.hpp"
#include "events/EventDispatcher.hpp"
#include "events/MaterialEvents.hpp"
#include "time/Timer.hpp"
#include <filesystem>
#include <algorithm>

namespace windows {

    MaterialEditorWindow::MaterialEditorWindow(const std::string& materialPath)
        : materialPath(materialPath)
        , graphEditor(std::make_unique<editor::graph::ShaderGraphEditor>())
        , previewController(std::make_unique<controllers::MaterialPreviewController>())
        , previewCamera(std::make_unique<editor::OrbitCamera>())
    {
        std::filesystem::path path(materialPath);
        windowTitle = "Material Editor: " + path.filename().string();

        // Configure camera for material preview sphere
        previewCamera->target = glm::vec3(0.0f);
        previewCamera->distance = 3.0f;
        previewCamera->yaw = 45.0f;
        previewCamera->pitch = 30.0f;
        previewCamera->minDistance = 1.5f;
        previewCamera->maxDistance = 10.0f;
    }

    MaterialEditorWindow::~MaterialEditorWindow() {
        if (graphEditor) {
            graphEditor->cleanUp();
        }
        if (previewController) {
            previewController->cleanUp();
        }
    }

    void MaterialEditorWindow::initEditor() {
        graphEditor->init();
        loadMaterial();

        if (materialData) {
            graphEditor->setGraph(&materialData->graph);
            graphEditor->setOnGraphChanged([this]() { onGraphChanged(); });
            graphEditor->navigateToContent();

            // Initial preview without custom shader (user must hit Compile to enable)
            updatePreviewMaterial(false);
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

        // Initialize node pins (not saved in .vfMat file, regenerated from node types)
        if (materialData) {
            for (auto& node : materialData->graph.nodes) {
                // Only initialize if pins are empty (loaded from file)
                if (node.inputs.empty() && node.outputs.empty()) {
                    editor::graph::ShaderNodeFactory::initializeNode(node, materialData->graph.nextPinId);
                }
            }
        }
    }

    void MaterialEditorWindow::saveMaterial() {
        if (!materialData) return;

        // Compile first to update cached shaders
        compileMaterial();

        if (material::MaterialManager::instance().saveMaterial(materialPath, *materialData)) {
            isDirty = false;
            vfLogInfo("Material saved: {}", materialPath);

            // Notify that material file was saved (for cache invalidation in render pipelines)
            events::material::MaterialFileSavedNotification notification;
            notification.materialPath = materialPath;
            events::EventDispatcher::instance().publish(notification);
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
            
            updatePreviewMaterial(true);
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

        // Use consistent title width to prevent window resizing when dirty state changes
        std::string title = windowTitle + (isDirty ? " *" : "  ");

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar;
        if (ImGui::Begin(title.c_str(), &isOpen, flags)) {
            if (isOpen) {
                drawToolbar();

                ImVec2 contentSize = ImGui::GetContentRegionAvail();

                // Layout: [Preview Panel] [Graph Editor]
                //         [Parameters   ] [Properties  ]

                float graphHeight = contentSize.y * 0.7f;
                float bottomHeight = contentSize.y - graphHeight - ImGui::GetStyle().ItemSpacing.y;

                // Top row
                ImGui::BeginChild("TopRow", ImVec2(0, graphHeight), false, ImGuiWindowFlags_NoScrollbar);
                {
                    ImVec2 topSize = ImGui::GetContentRegionAvail();

                    // Preview panel (left)
                    ImGui::BeginChild("PreviewPanel", ImVec2(previewPanelWidth, topSize.y), true);
                    drawPreviewPanel();
                    ImGui::EndChild();

                    ImGui::SameLine();

                    // Graph panel (right)
                    float graphWidth = topSize.x - previewPanelWidth - ImGui::GetStyle().ItemSpacing.x;
                    ImGui::BeginChild("GraphPanel", ImVec2(graphWidth, topSize.y), true,
                                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    drawGraphPanel();
                    ImGui::EndChild();
                }
                ImGui::EndChild();

                // Bottom row
                ImGui::BeginChild("BottomRow", ImVec2(0, bottomHeight), false, ImGuiWindowFlags_NoScrollbar);
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

        // Compile status (fixed width to prevent layout shifts)
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::PushItemWidth(120);
        if (showCompileError) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Compile Error!");
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(compileErrorMessage.c_str());
                ImGui::EndTooltip();
            }
        } else if (materialData && !materialData->needsRecompile) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Compiled      ");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Needs Compile");
        }
        ImGui::PopItemWidth();
        ImGui::EndGroup();

        ImGui::Separator();
    }

    void MaterialEditorWindow::initPreview() {
        previewController->init();
        previewCamera->updateMatrices();
        previewNeedsInit = false;
    }

    void MaterialEditorWindow::handlePreviewInput() {
        bool isHovered = ImGui::IsWindowHovered();

        // Track drag start/end
        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            isDraggingPreview = true;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            isDraggingPreview = false;
        }

        // Only process input when hovered
        if (!isHovered) return;

        ImGuiIO& io = ImGui::GetIO();

        // Scroll to zoom
        if (io.MouseWheel != 0.0f) {
            float zoomFactor = 1.0f - io.MouseWheel * previewCamera->zoomSensitivity * 0.1f;
            previewCamera->setDistance(previewCamera->distance * zoomFactor);
            previewCamera->updateMatrices();
        }

        // Left mouse drag to orbit - only if drag started in preview
        if (isDraggingPreview && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ImVec2 delta = io.MouseDelta;

            if (delta.x != 0.0f || delta.y != 0.0f) {
                previewCamera->yaw += delta.x * previewCamera->orbitSensitivity;
                previewCamera->pitch -= delta.y * previewCamera->orbitSensitivity;

                // Clamp pitch to avoid gimbal lock
                previewCamera->pitch = glm::clamp(previewCamera->pitch, -89.0f, 89.0f);

                previewCamera->updateMatrices();
            }
        }
    }

    void MaterialEditorWindow::updatePreviewMaterial(bool useCustomShader) {
        if (!materialData) return;

        controllers::PreviewMaterialParams params;
        params.useCustomShader = useCustomShader;

        // Find PBR Output node
        material::ShaderNode* pbrOutput = nullptr;
        for (auto& node : materialData->graph.nodes) {
            if (node.type == material::NodeType::PBROutput) {
                pbrOutput = &node;
                break;
            }
        }

        if (!pbrOutput) {
            previewController->setMaterialParams(params);
            return;
        }

        // Helper to find connected node's value for a given input pin name
        auto getConnectedValue = [this, pbrOutput](const std::string& pinName) -> std::optional<float> {
            // Find the input pin on PBR Output
            for (const auto& pin : pbrOutput->inputs) {
                if (pin.name == pinName) {
                    // Find link connected to this pin
                    for (const auto& link : materialData->graph.links) {
                        if (link.targetNodeId == pbrOutput->id && link.targetPin == pinName) {
                            // Find the source node
                            for (const auto& node : materialData->graph.nodes) {
                                if (node.id == link.sourceNodeId) {
                                    // Get value from source node's properties
                                    if (node.type == material::NodeType::ConstantScalar) {
                                        auto it = node.properties.find("value");
                                        if (it != node.properties.end() && std::holds_alternative<float>(it->second)) {
                                            return std::get<float>(it->second);
                                        }
                                    }
                                    break;
                                }
                            }
                            break;
                        }
                    }
                    break;
                }
            }
            return std::nullopt;
        };

        // Helper to get connected color value
        auto getConnectedColor = [this, pbrOutput](const std::string& pinName) -> std::optional<glm::vec4> {
            for (const auto& pin : pbrOutput->inputs) {
                if (pin.name == pinName) {
                    for (const auto& link : materialData->graph.links) {
                        if (link.targetNodeId == pbrOutput->id && link.targetPin == pinName) {
                            for (const auto& node : materialData->graph.nodes) {
                                if (node.id == link.sourceNodeId) {
                                    if (node.type == material::NodeType::ConstantColor) {
                                        auto it = node.properties.find("value");
                                        // ConstantColorNode stores glm::vec4
                                        if (it != node.properties.end() && std::holds_alternative<glm::vec4>(it->second)) {
                                            return std::get<glm::vec4>(it->second);
                                        }
                                    }
                                    else if (node.type == material::NodeType::ConstantVec3) {
                                        auto it = node.properties.find("value");
                                        if (it != node.properties.end() && std::holds_alternative<glm::vec3>(it->second)) {
                                            glm::vec3 vec = std::get<glm::vec3>(it->second);
                                            return glm::vec4(vec, 1.0f);
                                        }
                                    }
                                    break;
                                }
                            }
                            break;
                        }
                    }
                    break;
                }
            }
            return std::nullopt;
        };

        // Helper to get connected texture path from TextureSample node
        auto getConnectedTexturePath = [this, pbrOutput](const std::string& pinName) -> std::string {
            for (const auto& link : materialData->graph.links) {
                if (link.targetNodeId == pbrOutput->id && link.targetPin == pinName) {
                    for (const auto& node : materialData->graph.nodes) {
                        if (node.id == link.sourceNodeId && node.type == material::NodeType::TextureSample) {
                            auto it = node.properties.find("texturePath");
                            if (it != node.properties.end() && std::holds_alternative<std::string>(it->second)) {
                                return std::get<std::string>(it->second);
                            }
                        }
                    }
                }
            }
            return "";
        };

        // Extract scalar values from connected constant nodes
        if (auto albedo = getConnectedColor("Albedo")) {
            params.albedo = *albedo;
        }
        if (auto metallic = getConnectedValue("Metallic")) {
            params.metallic = *metallic;
        }
        if (auto roughness = getConnectedValue("Roughness")) {
            params.roughness = *roughness;
        }
        if (auto ao = getConnectedValue("AO")) {
            params.ao = *ao;
        }
        if (auto emission = getConnectedValue("Emission")) {
            params.emission = *emission;
        }

        // Extract texture paths from connected TextureSample nodes
        params.albedoTexturePath = getConnectedTexturePath("Albedo");
        params.metallicTexturePath = getConnectedTexturePath("Metallic");
        params.roughnessTexturePath = getConnectedTexturePath("Roughness");
        params.aoTexturePath = getConnectedTexturePath("AO");
        params.normalTexturePath = getConnectedTexturePath("Normal");
        params.emissionTexturePath = getConnectedTexturePath("Emission");

        // Pass material path for custom shader pipeline lookup
        params.materialPath = materialPath;

        // Pass material graph for dynamic evaluation (Time, Sin, Cos nodes)
        params.materialData = materialData;

        previewController->setMaterialParams(params);
    }

    void MaterialEditorWindow::drawPreviewPanel() {
        ImGui::Text("Preview");
        ImGui::Separator();

        // Initialize preview on first draw
        if (previewNeedsInit) {
            initPreview();
        }

        // Calculate square viewport size
        ImVec2 previewSize = ImGui::GetContentRegionAvail();
        float viewportSize = std::min(previewSize.x - 10.0f, previewSize.y - 100.0f);
        viewportSize = std::max(viewportSize, 100.0f);  // Minimum size

        ImGui::BeginChild("PreviewViewport", ImVec2(viewportSize, viewportSize), true,
                         ImGuiWindowFlags_NoScrollbar);
        {
            // Update camera aspect ratio
            previewCamera->setAspectRatio(1.0f);  // Square

            // Handle mouse input for orbit
            handlePreviewInput();

            // Note: Preview material is updated only when Compile is clicked
            // (see compileMaterial())

            // Update camera in controller
            previewController->updateCamera(
                previewCamera->getViewMatrix(),
                previewCamera->getProjectionMatrix(),
                previewCamera->getPosition(),
                static_cast<float>(engineTime::Timer::getElapsedTime())
            );

            // Render and display
            void* texture = previewController->render();

            // Check for shader compilation errors from the preview pipeline
            std::string shaderError = previewController->getLastShaderCompilationError();
            if (!shaderError.empty() && !showCompileError) {
                showCompileError = true;
                compileErrorMessage = "SPIR-V: " + shaderError;
            }

            if (texture) {
                ImVec2 size(viewportSize - 16, viewportSize - 16);
                ImGui::Image(texture, size);
            } else {
                ImGui::TextDisabled("Initializing preview...");
            }
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
        }
    }

    void MaterialEditorWindow::drawGraphPanel() {
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

                // Special handling for texture path property
                if (propName == "texturePath" && selectedNode->type == material::NodeType::TextureSample) {
                    ImGui::Text("Texture:");
                    ImGui::SameLine();

                    // Show current path (truncated if too long)
                    std::string displayPath = value.empty() ? "(None)" :
                        (value.length() > 30 ? "..." + value.substr(value.length() - 27) : value);
                    ImGui::TextDisabled("%s", displayPath.c_str());

                    // Browse button - opens native file dialog
                    if (ImGui::Button("Browse...")) {
                        nfd::FileDialog fileDialog;
                        std::vector<std::pair<std::wstring, std::wstring>> filters = {
                            {L"VF Image", L"*.vfImage"}
                        };
                        std::string selectedPath = fileDialog.openFileDialog(filters);
                        if (!selectedPath.empty()) {
                            propValue = selectedPath;
                            changed = true;
                        }
                    }

                    // Clear button
                    ImGui::SameLine();
                    if (ImGui::Button("Clear")) {
                        propValue = std::string("");
                        changed = true;
                    }
                } else {
                    // Default string input
                    char buffer[256];
                    strncpy_s(buffer, value.c_str(), sizeof(buffer) - 1);
                    if (ImGui::InputText(propName.c_str(), buffer, sizeof(buffer))) {
                        propValue = std::string(buffer);
                        changed = true;
                    }
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
