#include "ShaderGraphEditor.hpp"
#include "nodes/ShaderNode.hpp"
#include "imgui.h"

namespace ed = ax::NodeEditor;

namespace editor::graph {

    ShaderGraphEditor::ShaderGraphEditor() = default;

    ShaderGraphEditor::~ShaderGraphEditor() {
        cleanUp();
    }

    void ShaderGraphEditor::init() {
        if (editorContext) return;

        ed::Config config;
        config.SettingsFile = nullptr;  // Don't save to file
        editorContext = ed::CreateEditor(&config);
    }

    void ShaderGraphEditor::cleanUp() {
        if (editorContext) {
            ed::DestroyEditor(editorContext);
            editorContext = nullptr;
        }
    }

    void ShaderGraphEditor::setGraph(material::ShaderGraph* graph) {
        currentGraph = graph;
        needsPositionInit = true;  // Initialize positions on next draw
    }

    void ShaderGraphEditor::navigateToContent() {
        if (editorContext) {
            ed::SetCurrentEditor(editorContext);
            ed::NavigateToContent();
            ed::SetCurrentEditor(nullptr);
        }
    }

    void ShaderGraphEditor::draw() {
        if (!editorContext || !currentGraph) {
            ImGui::TextDisabled("No material loaded");
            return;
        }

        ed::SetCurrentEditor(editorContext);

        ed::Begin("ShaderGraphEditor");

        // Initialize node positions from graph data on first frame (must be after Begin)
        if (needsPositionInit) {
            for (const auto& node : currentGraph->nodes) {
                ed::SetNodePosition(toEditorNodeId(node.id), ImVec2(node.position.x, node.position.y));
            }
            needsPositionInit = false;
        }

        // Draw all nodes
        for (auto& node : currentGraph->nodes) {
            drawNode(node);
        }

        // Draw all links
        drawLinks();

        // Handle link creation
        handleCreation();

        // Handle deletion
        handleDeletion();

        // Check for context menu trigger (inside editor context)
        ed::Suspend();
        if (ed::ShowBackgroundContextMenu()) {
            newNodePosition = ed::ScreenToCanvas(ImGui::GetMousePos());
            showCreateNodeMenu = true;
        }
        ed::Resume();

        ed::End();

        // Update selection
        if (ed::HasSelectionChanged()) {
            ed::NodeId selectedNodes[1];
            int count = ed::GetSelectedNodes(selectedNodes, 1);
            selectedNodeId = count > 0 ? fromEditorNodeId(selectedNodes[0]) : 0;
        }

        // Update node positions in graph data (for saving)
        for (auto& node : currentGraph->nodes) {
            ImVec2 pos = ed::GetNodePosition(toEditorNodeId(node.id));
            if (pos.x != node.position.x || pos.y != node.position.y) {
                node.position.x = pos.x;
                node.position.y = pos.y;
            }
        }

        ed::SetCurrentEditor(nullptr);

        // Handle popup outside of editor context entirely
        handleContextMenu();
    }

    void ShaderGraphEditor::drawNode(material::ShaderNode& node) {
        ed::BeginNode(toEditorNodeId(node.id));

        // Header
        ImGui::TextUnformatted(node.name.empty() ? getNodeTypeName(node.type) : node.name.c_str());

        // Show color preview for ConstantColor nodes
        if (node.type == material::NodeType::ConstantColor) {
            glm::vec4 color(1.0f);
            auto it = node.properties.find("value");
            if (it != node.properties.end()) {
                if (auto* col = std::get_if<glm::vec4>(&it->second)) {
                    color = *col;
                }
            }
            // Just show a small color preview button (editing done in Properties panel)
            ImGui::ColorButton("##preview", ImVec4(color.x, color.y, color.z, color.w),
                ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
        }

        // Show texture path for TextureSample nodes
        if (node.type == material::NodeType::TextureSample) {
            std::string texPath = "";
            auto it = node.properties.find("texturePath");
            if (it != node.properties.end()) {
                if (auto* path = std::get_if<std::string>(&it->second)) {
                    texPath = *path;
                }
            }

            // Show truncated path or "No texture"
            std::string displayText = texPath.empty() ? "(No texture)" :
                (texPath.length() > 15 ? "..." + texPath.substr(texPath.length() - 12) : texPath);
            ImGui::TextDisabled("%s", displayText.c_str());
        }

        // Draw input pins with type-based icons
        for (const auto& pin : node.inputs) {
            ed::BeginPin(toEditorPinId(pin.id), ed::PinKind::Input);

            // Draw pin icon
            ImVec2 iconPos = ImGui::GetCursorScreenPos();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImU32 pinColor = getPinColor(pin.type);
            float iconSize = 8.0f;

            if (pin.type == material::PinType::Texture2D) {
                // Square for texture
                drawList->AddRectFilled(
                    ImVec2(iconPos.x, iconPos.y + 3),
                    ImVec2(iconPos.x + iconSize, iconPos.y + 3 + iconSize),
                    pinColor);
            } else {
                // Circle for scalar/vector types
                drawList->AddCircleFilled(
                    ImVec2(iconPos.x + iconSize/2, iconPos.y + 7),
                    iconSize/2, pinColor);
            }

            ImGui::Dummy(ImVec2(iconSize + 4, iconSize));
            ImGui::SameLine();
            ImGui::Text("%s", pin.name.c_str());
            ed::EndPin();
        }

        // Draw output pins with type-based icons
        for (const auto& pin : node.outputs) {
            ed::BeginPin(toEditorPinId(pin.id), ed::PinKind::Output);

            ImGui::Text("%s", pin.name.c_str());
            ImGui::SameLine();

            // Draw pin icon
            ImVec2 iconPos = ImGui::GetCursorScreenPos();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImU32 pinColor = getPinColor(pin.type);
            float iconSize = 8.0f;

            if (pin.type == material::PinType::Texture2D) {
                // Square for texture
                drawList->AddRectFilled(
                    ImVec2(iconPos.x, iconPos.y + 3),
                    ImVec2(iconPos.x + iconSize, iconPos.y + 3 + iconSize),
                    pinColor);
            } else {
                // Circle for scalar/vector types
                drawList->AddCircleFilled(
                    ImVec2(iconPos.x + iconSize/2, iconPos.y + 7),
                    iconSize/2, pinColor);
            }

            ImGui::Dummy(ImVec2(iconSize, iconSize));
            ed::EndPin();
        }

        ed::EndNode();
    }

    void ShaderGraphEditor::drawLinks() {
        for (const auto& link : currentGraph->links) {
            // Find source and target pins to get their types for coloring
            const material::NodePin* sourcePin = findPin(link.sourceNodeId);
            ImU32 color = sourcePin ? getPinColor(sourcePin->type) : IM_COL32(255, 255, 255, 200);

            // We need to find the actual pin IDs from node/pin name
            // For now, use a simplified approach - find pins by iterating
            uint32_t sourcePinId = 0;
            uint32_t targetPinId = 0;

            for (const auto& node : currentGraph->nodes) {
                if (node.id == link.sourceNodeId) {
                    for (const auto& pin : node.outputs) {
                        if (pin.name == link.sourcePin) {
                            sourcePinId = pin.id;
                            break;
                        }
                    }
                }
                if (node.id == link.targetNodeId) {
                    for (const auto& pin : node.inputs) {
                        if (pin.name == link.targetPin) {
                            targetPinId = pin.id;
                            break;
                        }
                    }
                }
            }

            if (sourcePinId && targetPinId) {
                ed::Link(toEditorLinkId(link.id), toEditorPinId(sourcePinId), toEditorPinId(targetPinId),
                        ImColor(color), 2.0f);
            }
        }
    }

    void ShaderGraphEditor::handleCreation() {
        if (ed::BeginCreate()) {
            ed::PinId startPinId, endPinId;
            if (ed::QueryNewLink(&startPinId, &endPinId)) {
                uint32_t startId = fromEditorPinId(startPinId);
                uint32_t endId = fromEditorPinId(endPinId);

                if (startId && endId && canCreateLink(startId, endId)) {
                    if (ed::AcceptNewItem(ImColor(128, 255, 128), 4.0f)) {
                        // Create the link
                        material::NodeLink newLink;
                        newLink.id = currentGraph->nextLinkId++;

                        // Find source node and pin
                        for (const auto& node : currentGraph->nodes) {
                            for (const auto& pin : node.outputs) {
                                if (pin.id == startId) {
                                    newLink.sourceNodeId = node.id;
                                    newLink.sourcePin = pin.name;
                                    break;
                                }
                            }
                            for (const auto& pin : node.inputs) {
                                if (pin.id == startId) {
                                    newLink.sourceNodeId = node.id;
                                    newLink.sourcePin = pin.name;
                                    break;
                                }
                            }
                        }

                        // Find target node and pin
                        for (const auto& node : currentGraph->nodes) {
                            for (const auto& pin : node.inputs) {
                                if (pin.id == endId) {
                                    newLink.targetNodeId = node.id;
                                    newLink.targetPin = pin.name;
                                    break;
                                }
                            }
                            for (const auto& pin : node.outputs) {
                                if (pin.id == endId) {
                                    newLink.targetNodeId = node.id;
                                    newLink.targetPin = pin.name;
                                    break;
                                }
                            }
                        }

                        currentGraph->links.push_back(newLink);
                        if (onGraphChanged) onGraphChanged();
                    }
                } else {
                    ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                }
            }

            ed::PinId pinId;
            if (ed::QueryNewNode(&pinId)) {
                // Reject creating nodes by dragging from pins - use right-click menu instead
                ed::RejectNewItem();
            }
        }
        ed::EndCreate();
    }

    void ShaderGraphEditor::handleDeletion() {
        if (ed::BeginDelete()) {
            ed::LinkId linkId;
            while (ed::QueryDeletedLink(&linkId)) {
                if (ed::AcceptDeletedItem()) {
                    uint32_t id = fromEditorLinkId(linkId);
                    auto it = std::find_if(currentGraph->links.begin(), currentGraph->links.end(),
                        [id](const material::NodeLink& link) { return link.id == id; });
                    if (it != currentGraph->links.end()) {
                        currentGraph->links.erase(it);
                        if (onGraphChanged) onGraphChanged();
                    }
                }
            }

            ed::NodeId nodeId;
            while (ed::QueryDeletedNode(&nodeId)) {
                uint32_t id = fromEditorNodeId(nodeId);
                // Don't allow deleting the PBR Output node
                auto node = currentGraph->findNode(id);
                if (node && node->type == material::NodeType::PBROutput) {
                    ed::RejectDeletedItem();
                } else if (ed::AcceptDeletedItem()) {
                    // Remove all links connected to this node
                    currentGraph->links.erase(
                        std::remove_if(currentGraph->links.begin(), currentGraph->links.end(),
                            [id](const material::NodeLink& link) {
                                return link.sourceNodeId == id || link.targetNodeId == id;
                            }),
                        currentGraph->links.end()
                    );

                    // Remove the node
                    auto it = std::find_if(currentGraph->nodes.begin(), currentGraph->nodes.end(),
                        [id](const material::ShaderNode& n) { return n.id == id; });
                    if (it != currentGraph->nodes.end()) {
                        currentGraph->nodes.erase(it);
                        if (onGraphChanged) onGraphChanged();
                    }
                }
            }
        }
        ed::EndDelete();
    }

    void ShaderGraphEditor::handleContextMenu() {
        // This is called outside ed::SetCurrentEditor context
        if (showCreateNodeMenu) {
            ImGui::OpenPopup("AddNode");
            showCreateNodeMenu = false;
        }

        if (ImGui::BeginPopup("AddNode")) {
            // Constants
            if (ImGui::BeginMenu("Constants")) {
                if (ImGui::MenuItem("Scalar")) {
                    createNode(material::NodeType::ConstantScalar, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Vector2")) {
                    createNode(material::NodeType::ConstantVec2, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Vector3")) {
                    createNode(material::NodeType::ConstantVec3, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Color")) {
                    createNode(material::NodeType::ConstantColor, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }

            // Math
            if (ImGui::BeginMenu("Math")) {
                if (ImGui::MenuItem("Add")) {
                    createNode(material::NodeType::Add, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Subtract")) {
                    createNode(material::NodeType::Subtract, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Multiply")) {
                    createNode(material::NodeType::Multiply, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Divide")) {
                    createNode(material::NodeType::Divide, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Power")) {
                    createNode(material::NodeType::Power, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Lerp")) {
                    createNode(material::NodeType::Lerp, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Clamp")) {
                    createNode(material::NodeType::Clamp, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Saturate")) {
                    createNode(material::NodeType::Saturate, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("One Minus")) {
                    createNode(material::NodeType::OneMinus, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Abs")) {
                    createNode(material::NodeType::Abs, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }

            // Trig
            if (ImGui::BeginMenu("Trigonometry")) {
                if (ImGui::MenuItem("Sin")) {
                    createNode(material::NodeType::Sin, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Cos")) {
                    createNode(material::NodeType::Cos, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }

            // Vector
            if (ImGui::BeginMenu("Vector")) {
                if (ImGui::MenuItem("Make Vec3")) {
                    createNode(material::NodeType::MakeVec3, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Normalize")) {
                    createNode(material::NodeType::Normalize, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Length")) {
                    createNode(material::NodeType::Length, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Dot")) {
                    createNode(material::NodeType::Dot, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Cross")) {
                    createNode(material::NodeType::Cross, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Fresnel")) {
                    createNode(material::NodeType::Fresnel, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }

            // Inputs
            if (ImGui::BeginMenu("Inputs")) {
                if (ImGui::MenuItem("UV")) {
                    createNode(material::NodeType::VertexUV, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Normal")) {
                    createNode(material::NodeType::VertexNormal, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Position")) {
                    createNode(material::NodeType::VertexPosition, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Camera Position")) {
                    createNode(material::NodeType::CameraPosition, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Time")) {
                    createNode(material::NodeType::Time, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }

            // Texture
            if (ImGui::BeginMenu("Texture")) {
                if (ImGui::MenuItem("Texture Sample")) {
                    createNode(material::NodeType::TextureSample, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }

            ImGui::EndPopup();
        }
    }

    void ShaderGraphEditor::createNode(material::NodeType type, const ImVec2& position) {
        material::ShaderNode node;
        node.id = currentGraph->nextNodeId++;
        node.type = type;
        node.position = glm::vec2(position.x, position.y);
        node.name = getNodeTypeName(type);

        // Initialize pins based on node type using factory
        ShaderNodeFactory::initializeNode(node, currentGraph->nextPinId);

        currentGraph->nodes.push_back(node);

        // Set node position in editor (need to set editor context since we might be outside it)
        ed::SetCurrentEditor(editorContext);
        ed::SetNodePosition(toEditorNodeId(node.id), position);
        ed::SetCurrentEditor(nullptr);

        if (onGraphChanged) onGraphChanged();
    }

    const material::NodePin* ShaderGraphEditor::findPin(uint32_t pinId) const {
        for (const auto& node : currentGraph->nodes) {
            for (const auto& pin : node.inputs) {
                if (pin.id == pinId) return &pin;
            }
            for (const auto& pin : node.outputs) {
                if (pin.id == pinId) return &pin;
            }
        }
        return nullptr;
    }

    material::ShaderNode* ShaderGraphEditor::findNodeByPinId(uint32_t pinId) {
        for (auto& node : currentGraph->nodes) {
            for (const auto& pin : node.inputs) {
                if (pin.id == pinId) return &node;
            }
            for (const auto& pin : node.outputs) {
                if (pin.id == pinId) return &node;
            }
        }
        return nullptr;
    }

    bool ShaderGraphEditor::canCreateLink(uint32_t startPinId, uint32_t endPinId) const {
        const material::NodePin* startPin = findPin(startPinId);
        const material::NodePin* endPin = findPin(endPinId);

        if (!startPin || !endPin) return false;

        // Can't connect input to input or output to output
        if (startPin->kind == endPin->kind) return false;

        // For now, allow any type connection (auto-conversion in shader)
        return true;
    }

    ImU32 ShaderGraphEditor::getPinColor(material::PinType type) const {
        switch (type) {
            case material::PinType::Float:    return IM_COL32(150, 200, 150, 255);
            case material::PinType::Vec2:     return IM_COL32(150, 200, 255, 255);
            case material::PinType::Vec3:     return IM_COL32(255, 200, 150, 255);
            case material::PinType::Vec4:     return IM_COL32(255, 150, 200, 255);
            case material::PinType::Texture2D: return IM_COL32(200, 150, 255, 255);
            default:                          return IM_COL32(200, 200, 200, 255);
        }
    }

    ImU32 ShaderGraphEditor::getNodeHeaderColor(material::NodeType type) const {
        switch (type) {
            case material::NodeType::PBROutput:
                return IM_COL32(150, 80, 80, 255);  // Red for output
            case material::NodeType::ConstantScalar:
            case material::NodeType::ConstantVec2:
            case material::NodeType::ConstantVec3:
            case material::NodeType::ConstantColor:
                return IM_COL32(80, 150, 80, 255);  // Green for constants
            case material::NodeType::Add:
            case material::NodeType::Subtract:
            case material::NodeType::Multiply:
            case material::NodeType::Divide:
            case material::NodeType::Lerp:
            case material::NodeType::Power:
                return IM_COL32(80, 80, 150, 255);  // Blue for math
            case material::NodeType::VertexPosition:
            case material::NodeType::VertexNormal:
            case material::NodeType::VertexUV:
            case material::NodeType::Time:
            case material::NodeType::CameraPosition:
                return IM_COL32(150, 150, 80, 255);  // Yellow for inputs
            case material::NodeType::TextureSample:
                return IM_COL32(180, 100, 180, 255);  // Purple for textures
            default:
                return IM_COL32(100, 100, 100, 255);
        }
    }

    const char* ShaderGraphEditor::getNodeTypeName(material::NodeType type) const {
        switch (type) {
            case material::NodeType::PBROutput: return "PBR Output";
            case material::NodeType::ConstantScalar: return "Scalar";
            case material::NodeType::ConstantVec2: return "Vector2";
            case material::NodeType::ConstantVec3: return "Vector3";
            case material::NodeType::ConstantColor: return "Color";
            case material::NodeType::Add: return "Add";
            case material::NodeType::Subtract: return "Subtract";
            case material::NodeType::Multiply: return "Multiply";
            case material::NodeType::Divide: return "Divide";
            case material::NodeType::Power: return "Power";
            case material::NodeType::Lerp: return "Lerp";
            case material::NodeType::Clamp: return "Clamp";
            case material::NodeType::Saturate: return "Saturate";
            case material::NodeType::OneMinus: return "One Minus";
            case material::NodeType::Abs: return "Abs";
            case material::NodeType::Floor: return "Floor";
            case material::NodeType::Ceil: return "Ceil";
            case material::NodeType::Fract: return "Fract";
            case material::NodeType::Sin: return "Sin";
            case material::NodeType::Cos: return "Cos";
            case material::NodeType::Dot: return "Dot";
            case material::NodeType::Cross: return "Cross";
            case material::NodeType::Normalize: return "Normalize";
            case material::NodeType::Length: return "Length";
            case material::NodeType::MakeVec2: return "Make Vec2";
            case material::NodeType::MakeVec3: return "Make Vec3";
            case material::NodeType::MakeVec4: return "Make Vec4";
            case material::NodeType::SplitVec2: return "Split Vec2";
            case material::NodeType::SplitVec3: return "Split Vec3";
            case material::NodeType::SplitVec4: return "Split Vec4";
            case material::NodeType::Fresnel: return "Fresnel";
            case material::NodeType::VertexPosition: return "Position";
            case material::NodeType::VertexNormal: return "Normal";
            case material::NodeType::VertexUV: return "UV";
            case material::NodeType::Time: return "Time";
            case material::NodeType::CameraPosition: return "Camera Pos";
            case material::NodeType::TextureSample: return "Texture Sample";
            default: return "Unknown";
        }
    }

}
