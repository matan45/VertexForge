#include "ShaderGraphEditor.hpp"
#include "nodes/ShaderNode.hpp"
#include "imgui.h"
#include <algorithm>

namespace ed = ax::NodeEditor;

namespace editor::graph {

    void ShaderGraphEditor::handleCreation() {
        if (ed::BeginCreate()) {
            ed::PinId startPinId, endPinId;
            if (ed::QueryNewLink(&startPinId, &endPinId)) {
                uint32_t startId = fromEditorPinId(startPinId);
                uint32_t endId = fromEditorPinId(endPinId);

                if (startId && endId && canCreateLink(startId, endId)) {
                    if (ed::AcceptNewItem(ImColor(128, 255, 128), 4.0f)) {
                        material::NodeLink newLink;
                        newLink.id = currentGraph->nextLinkId++;

                        // Find both pins and determine correct direction
                        const material::NodePin* startPin = findPin(startId);
                        const material::NodePin* endPin = findPin(endId);

                        // Determine which is source (output) and which is target (input)
                        uint32_t sourcePinId = startId;
                        uint32_t targetPinId = endId;
                        if (startPin && startPin->kind == material::PinKind::Input) {
                            // User dragged from input to output, swap them
                            sourcePinId = endId;
                            targetPinId = startId;
                        }

                        // Find source node and pin (must be an output pin)
                        for (const auto& node : currentGraph->nodes) {
                            for (const auto& pin : node.outputs) {
                                if (pin.id == sourcePinId) {
                                    newLink.sourceNodeId = node.id;
                                    newLink.sourcePin = pin.name;
                                    break;
                                }
                            }
                        }

                        // Find target node and pin (must be an input pin)
                        for (const auto& node : currentGraph->nodes) {
                            for (const auto& pin : node.inputs) {
                                if (pin.id == targetPinId) {
                                    newLink.targetNodeId = node.id;
                                    newLink.targetPin = pin.name;
                                    break;
                                }
                            }
                        }

                        // Remove any existing link to this input pin (input pins only accept one connection)
                        currentGraph->links.erase(
                            std::remove_if(currentGraph->links.begin(), currentGraph->links.end(),
                                [&newLink](const material::NodeLink& existing) {
                                    return existing.targetNodeId == newLink.targetNodeId &&
                                           existing.targetPin == newLink.targetPin;
                                }),
                            currentGraph->links.end());

                        currentGraph->links.push_back(newLink);
                        if (onGraphChanged) onGraphChanged();
                    }
                } else {
                    ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                }
            }

            ed::PinId pinId;
            if (ed::QueryNewNode(&pinId)) {
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
                if (ImGui::MenuItem("Mix Color")) {
                    createNode(material::NodeType::MixColor, newNodePosition);
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
                if (ImGui::MenuItem("Make Vec2")) {
                    createNode(material::NodeType::MakeVec2, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
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
                if (ImGui::MenuItem("ORM Sample")) {
                    createNode(material::NodeType::OrmSample, newNodePosition);
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

        // Set node position in editor
        ed::SetCurrentEditor(editorContext);
        ed::SetNodePosition(toEditorNodeId(node.id), position);
        ed::SetCurrentEditor(nullptr);

        if (onGraphChanged) onGraphChanged();
    }

}
