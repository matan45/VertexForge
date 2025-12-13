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

        // Handle context menu
        handleContextMenu();

        ed::End();

        // Update selection
        if (ed::HasSelectionChanged()) {
            ed::NodeId selectedNodes[1];
            int count = ed::GetSelectedNodes(selectedNodes, 1);
            selectedNodeId = count > 0 ? fromEditorNodeId(selectedNodes[0]) : 0;
        }

        ed::SetCurrentEditor(nullptr);
    }

    void ShaderGraphEditor::drawNode(material::ShaderNode& node) {
        ed::BeginNode(toEditorNodeId(node.id));

        // Header
        ImU32 headerColor = getNodeHeaderColor(node.type);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
        ImGui::TextUnformatted(node.name.empty() ? getNodeTypeName(node.type) : node.name.c_str());
        ImGui::PopStyleColor();

        ImGui::Separator();

        // Draw input pins
        for (const auto& pin : node.inputs) {
            drawPin(pin, false);
        }

        // Draw output pins
        for (const auto& pin : node.outputs) {
            drawPin(pin, true);
        }

        // Draw node-specific properties
        if (node.type == material::NodeType::ConstantScalar) {
            auto it = node.properties.find("value");
            if (it != node.properties.end() && std::holds_alternative<float>(it->second)) {
                float value = std::get<float>(it->second);
                ImGui::SetNextItemWidth(80);
                if (ImGui::DragFloat("##value", &value, 0.01f, 0.0f, 1.0f, "%.3f")) {
                    it->second = value;
                    if (onGraphChanged) onGraphChanged();
                }
            }
        }
        else if (node.type == material::NodeType::ConstantVec3 || node.type == material::NodeType::ConstantColor) {
            auto it = node.properties.find("value");
            if (it != node.properties.end() && std::holds_alternative<glm::vec3>(it->second)) {
                glm::vec3 value = std::get<glm::vec3>(it->second);
                ImGui::SetNextItemWidth(150);
                bool changed = false;
                if (node.type == material::NodeType::ConstantColor) {
                    changed = ImGui::ColorEdit3("##color", &value.x, ImGuiColorEditFlags_NoInputs);
                } else {
                    changed = ImGui::DragFloat3("##vec3", &value.x, 0.01f);
                }
                if (changed) {
                    it->second = value;
                    if (onGraphChanged) onGraphChanged();
                }
            }
        }

        ed::EndNode();
    }

    void ShaderGraphEditor::drawPin(const material::NodePin& pin, bool isOutput) {
        ImU32 color = getPinColor(pin.type);

        if (isOutput) {
            // Right-aligned output pins
            float nodeWidth = ImGui::GetContentRegionAvail().x;
            ImGui::Indent(nodeWidth - 80);
        }

        ed::BeginPin(toEditorPinId(pin.id), isOutput ? ed::PinKind::Output : ed::PinKind::Input);

        // Draw pin circle
        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        float pinRadius = 5.0f;
        ImVec2 pinCenter = ImVec2(cursorPos.x + (isOutput ? 70 : 8), cursorPos.y + 8);

        drawList->AddCircleFilled(pinCenter, pinRadius, color);
        drawList->AddCircle(pinCenter, pinRadius, IM_COL32(200, 200, 200, 255));

        // Pin label
        if (isOutput) {
            ImGui::TextUnformatted(pin.name.c_str());
        } else {
            ImGui::Dummy(ImVec2(15, 0));
            ImGui::SameLine();
            ImGui::TextUnformatted(pin.name.c_str());
        }

        ed::EndPin();

        if (isOutput) {
            ImGui::Unindent();
        }
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
                newNodeLinkPin = pinId;
                if (ed::AcceptNewItem()) {
                    showCreateNodeMenu = true;
                    newNodePosition = ImGui::GetMousePos();
                    ed::Suspend();
                }
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
        // Background context menu
        if (ed::ShowBackgroundContextMenu()) {
            ImGui::OpenPopup("CreateNodeMenu");
            newNodePosition = ImGui::GetMousePos();
        }

        // Node creation popup
        ed::Suspend();
        if (ImGui::BeginPopup("CreateNodeMenu")) {
            if (ImGui::BeginMenu("Constants")) {
                if (ImGui::MenuItem("Scalar")) createNode(material::NodeType::ConstantScalar, newNodePosition);
                if (ImGui::MenuItem("Vector2")) createNode(material::NodeType::ConstantVec2, newNodePosition);
                if (ImGui::MenuItem("Vector3")) createNode(material::NodeType::ConstantVec3, newNodePosition);
                if (ImGui::MenuItem("Color")) createNode(material::NodeType::ConstantColor, newNodePosition);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Math")) {
                if (ImGui::MenuItem("Add")) createNode(material::NodeType::Add, newNodePosition);
                if (ImGui::MenuItem("Subtract")) createNode(material::NodeType::Subtract, newNodePosition);
                if (ImGui::MenuItem("Multiply")) createNode(material::NodeType::Multiply, newNodePosition);
                if (ImGui::MenuItem("Divide")) createNode(material::NodeType::Divide, newNodePosition);
                if (ImGui::MenuItem("Lerp")) createNode(material::NodeType::Lerp, newNodePosition);
                if (ImGui::MenuItem("Clamp")) createNode(material::NodeType::Clamp, newNodePosition);
                if (ImGui::MenuItem("Saturate")) createNode(material::NodeType::Saturate, newNodePosition);
                if (ImGui::MenuItem("One Minus")) createNode(material::NodeType::OneMinus, newNodePosition);
                if (ImGui::MenuItem("Power")) createNode(material::NodeType::Power, newNodePosition);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Trigonometry")) {
                if (ImGui::MenuItem("Sin")) createNode(material::NodeType::Sin, newNodePosition);
                if (ImGui::MenuItem("Cos")) createNode(material::NodeType::Cos, newNodePosition);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Vector")) {
                if (ImGui::MenuItem("Dot Product")) createNode(material::NodeType::Dot, newNodePosition);
                if (ImGui::MenuItem("Cross Product")) createNode(material::NodeType::Cross, newNodePosition);
                if (ImGui::MenuItem("Normalize")) createNode(material::NodeType::Normalize, newNodePosition);
                if (ImGui::MenuItem("Length")) createNode(material::NodeType::Length, newNodePosition);
                if (ImGui::MenuItem("Make Vec3")) createNode(material::NodeType::MakeVec3, newNodePosition);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Input")) {
                if (ImGui::MenuItem("UV")) createNode(material::NodeType::VertexUV, newNodePosition);
                if (ImGui::MenuItem("Normal")) createNode(material::NodeType::VertexNormal, newNodePosition);
                if (ImGui::MenuItem("Position")) createNode(material::NodeType::VertexPosition, newNodePosition);
                if (ImGui::MenuItem("Time")) createNode(material::NodeType::Time, newNodePosition);
                if (ImGui::MenuItem("Camera Position")) createNode(material::NodeType::CameraPosition, newNodePosition);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Utility")) {
                if (ImGui::MenuItem("Fresnel")) createNode(material::NodeType::Fresnel, newNodePosition);
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
        ed::Resume();
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

        // Set node position in editor
        ed::SetNodePosition(toEditorNodeId(node.id), position);

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
            default: return "Unknown";
        }
    }

}
