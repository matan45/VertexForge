#include "VFXGraphEditor.hpp"
#include "imgui.h"
#include <algorithm>

namespace ed = ax::NodeEditor;

namespace editor::graph {

    bool VFXGraphEditor::canCreateLink(uint32_t startPinId, uint32_t endPinId) const {
        uint32_t startNodeId = getNodeIdFromPinId(startPinId);
        uint32_t endNodeId = getNodeIdFromPinId(endPinId);

        if (startNodeId == endNodeId) return false;

        const vfx::VFXNode* startNode = currentGraph->findNode(startNodeId);
        const vfx::VFXNode* endNode = currentGraph->findNode(endNodeId);

        if (!startNode || !endNode) return false;

        bool startIsOutput = isOutputPin(startPinId);
        bool endIsOutput = isOutputPin(endPinId);

        // Can't connect same pin types
        if (startIsOutput == endIsOutput) return false;

        const vfx::VFXNode* sourceNode = startIsOutput ? startNode : endNode;
        const vfx::VFXNode* targetNode = startIsOutput ? endNode : startNode;

        // VK-238: Source must have an output pin (Emitter or Modifier)
        if (!vfx::hasOutputPin(sourceNode->type)) return false;

        // VK-238: Target must have an input pin (OutSystem or Modifier)
        if (!vfx::hasInputPin(targetNode->type)) return false;

        return true;
    }

    void VFXGraphEditor::handleCreation() {
        if (ed::BeginCreate()) {
            ed::PinId startPinId, endPinId;
            if (ed::QueryNewLink(&startPinId, &endPinId)) {
                uint32_t startId = fromEditorPinId(startPinId);
                uint32_t endId = fromEditorPinId(endPinId);

                if (startId && endId && canCreateLink(startId, endId)) {
                    if (ed::AcceptNewItem(ImColor(128, 255, 128), 4.0f)) {
                        vfx::VFXNodeLink newLink;
                        newLink.id = currentGraph->nextLinkId++;

                        bool startIsOutput = isOutputPin(startId);
                        uint32_t sourceNodeId = startIsOutput ? getNodeIdFromPinId(startId) : getNodeIdFromPinId(endId);
                        uint32_t targetNodeId = startIsOutput ? getNodeIdFromPinId(endId) : getNodeIdFromPinId(startId);

                        newLink.sourceNodeId = sourceNodeId;
                        newLink.targetNodeId = targetNodeId;
                        newLink.sourcePin = "Output";
                        newLink.targetPin = "Input";

                        // Remove any existing link to this target (single connection allowed)
                        currentGraph->links.erase(
                            std::remove_if(currentGraph->links.begin(), currentGraph->links.end(),
                                [targetNodeId](const vfx::VFXNodeLink& existing) {
                                    return existing.targetNodeId == targetNodeId;
                                }),
                            currentGraph->links.end());

                        // Remove any existing link from this source (single connection from emitter)
                        currentGraph->links.erase(
                            std::remove_if(currentGraph->links.begin(), currentGraph->links.end(),
                                [sourceNodeId](const vfx::VFXNodeLink& existing) {
                                    return existing.sourceNodeId == sourceNodeId;
                                }),
                            currentGraph->links.end());

                        currentGraph->links.push_back(newLink);
                        if (onGraphChanged) onGraphChanged();
                    }
                } else {
                    ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);

                    if (startId && endId) {
                        uint32_t startNodeId = getNodeIdFromPinId(startId);
                        uint32_t endNodeId = getNodeIdFromPinId(endId);

                        std::string errorMsg;
                        if (startNodeId == endNodeId) {
                            errorMsg = "Cannot connect node to itself";
                        } else if (isOutputPin(startId) == isOutputPin(endId)) {
                            errorMsg = "Cannot connect same pin types";
                        } else {
                            errorMsg = "Invalid connection";
                        }

                        ImGui::BeginTooltip();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                        ImGui::TextUnformatted(errorMsg.c_str());
                        ImGui::PopStyleColor();
                        ImGui::EndTooltip();
                    }
                }
            }

            ed::PinId pinId;
            if (ed::QueryNewNode(&pinId)) {
                // Don't allow creating nodes from pins in Part 1
                ed::RejectNewItem();
            }
        }
        ed::EndCreate();
    }

    void VFXGraphEditor::handleDeletion() {
        if (ed::BeginDelete()) {
            ed::LinkId linkId;
            while (ed::QueryDeletedLink(&linkId)) {
                if (ed::AcceptDeletedItem()) {
                    uint32_t id = fromEditorLinkId(linkId);
                    auto it = std::find_if(currentGraph->links.begin(), currentGraph->links.end(),
                        [id](const vfx::VFXNodeLink& link) { return link.id == id; });
                    if (it != currentGraph->links.end()) {
                        currentGraph->links.erase(it);
                        if (onGraphChanged) onGraphChanged();
                    }
                }
            }

            // Handle node deletion - reject deletion of Emitter and OutSystem nodes (Part 1)
            ed::NodeId nodeId;
            while (ed::QueryDeletedNode(&nodeId)) {
                uint32_t id = fromEditorNodeId(nodeId);
                auto node = currentGraph->findNode(id);
                if (node) {
                    // Reject deletion of core nodes in Part 1
                    if (node->type == vfx::VFXNodeType::Emitter ||
                        node->type == vfx::VFXNodeType::OutSystem) {
                        ed::RejectDeletedItem();
                    } else {
                        if (ed::AcceptDeletedItem()) {
                            currentGraph->links.erase(
                                std::remove_if(currentGraph->links.begin(), currentGraph->links.end(),
                                    [id](const vfx::VFXNodeLink& link) {
                                        return link.sourceNodeId == id || link.targetNodeId == id;
                                    }),
                                currentGraph->links.end()
                            );

                            auto it = std::find_if(currentGraph->nodes.begin(), currentGraph->nodes.end(),
                                [id](const vfx::VFXNode& n) { return n.id == id; });
                            if (it != currentGraph->nodes.end()) {
                                currentGraph->nodes.erase(it);
                                if (onGraphChanged) onGraphChanged();
                            }
                        }
                    }
                }
            }
        }
        ed::EndDelete();
    }

    // VK-238: Helper function to create a modifier node with default properties
    static void initializeModifierProperties(vfx::VFXNode& node) {
        switch (node.type) {
            case vfx::VFXNodeType::ColorOverLifetime:
                node.properties["startColor"] = vfx::VFXProperty{
                    "startColor", vfx::VFXPropertyType::Color,
                    vfx::ModifierDefaults::COLOR_START, 0.0f, 1.0f
                };
                node.properties["endColor"] = vfx::VFXProperty{
                    "endColor", vfx::VFXPropertyType::Color,
                    vfx::ModifierDefaults::COLOR_END, 0.0f, 1.0f
                };
                break;

            case vfx::VFXNodeType::SizeOverLifetime:
                node.properties["startMultiplier"] = vfx::VFXProperty{
                    "startMultiplier", vfx::VFXPropertyType::Float,
                    vfx::ModifierDefaults::SIZE_START_MULTIPLIER, 0.0f, 10.0f
                };
                node.properties["endMultiplier"] = vfx::VFXProperty{
                    "endMultiplier", vfx::VFXPropertyType::Float,
                    vfx::ModifierDefaults::SIZE_END_MULTIPLIER, 0.0f, 10.0f
                };
                break;

            case vfx::VFXNodeType::SpeedOverLifetime:
                node.properties["startMultiplier"] = vfx::VFXProperty{
                    "startMultiplier", vfx::VFXPropertyType::Float,
                    vfx::ModifierDefaults::SPEED_START_MULTIPLIER, 0.0f, 10.0f
                };
                node.properties["endMultiplier"] = vfx::VFXProperty{
                    "endMultiplier", vfx::VFXPropertyType::Float,
                    vfx::ModifierDefaults::SPEED_END_MULTIPLIER, 0.0f, 10.0f
                };
                break;

            case vfx::VFXNodeType::RotationOverLifetime:
                node.properties["angularVelocity"] = vfx::VFXProperty{
                    "angularVelocity", vfx::VFXPropertyType::Float,
                    vfx::ModifierDefaults::ANGULAR_VELOCITY, -720.0f, 720.0f
                };
                break;

            default:
                break;
        }
    }

    // VK-239: Helper function to create a force node with default properties
    static void initializeForceProperties(vfx::VFXNode& node) {
        switch (node.type) {
            case vfx::VFXNodeType::ForceGravity:
                node.properties["direction"] = vfx::VFXProperty{
                    "direction", vfx::VFXPropertyType::Vec3,
                    vfx::ForceDefaults::GRAVITY_DIRECTION, -10.0f, 10.0f
                };
                node.properties["strength"] = vfx::VFXProperty{
                    "strength", vfx::VFXPropertyType::Float,
                    vfx::ForceDefaults::GRAVITY_STRENGTH, 0.0f, 50.0f
                };
                node.properties["localSpace"] = vfx::VFXProperty{
                    "localSpace", vfx::VFXPropertyType::Bool,
                    false, 0.0f, 1.0f
                };
                break;

            case vfx::VFXNodeType::ForceWind:
                node.properties["direction"] = vfx::VFXProperty{
                    "direction", vfx::VFXPropertyType::Vec3,
                    vfx::ForceDefaults::WIND_DIRECTION, -10.0f, 10.0f
                };
                node.properties["strength"] = vfx::VFXProperty{
                    "strength", vfx::VFXPropertyType::Float,
                    vfx::ForceDefaults::WIND_STRENGTH, 0.0f, 50.0f
                };
                node.properties["noiseStrength"] = vfx::VFXProperty{
                    "noiseStrength", vfx::VFXPropertyType::Float,
                    vfx::ForceDefaults::WIND_NOISE_STRENGTH, 0.0f, 10.0f
                };
                node.properties["noiseFrequency"] = vfx::VFXProperty{
                    "noiseFrequency", vfx::VFXPropertyType::Float,
                    vfx::ForceDefaults::WIND_NOISE_FREQUENCY, 0.1f, 10.0f
                };
                node.properties["localSpace"] = vfx::VFXProperty{
                    "localSpace", vfx::VFXPropertyType::Bool,
                    false, 0.0f, 1.0f
                };
                break;

            case vfx::VFXNodeType::ForceTurbulence:
                node.properties["strength"] = vfx::VFXProperty{
                    "strength", vfx::VFXPropertyType::Float,
                    vfx::ForceDefaults::TURBULENCE_STRENGTH, 0.0f, 50.0f
                };
                node.properties["frequency"] = vfx::VFXProperty{
                    "frequency", vfx::VFXPropertyType::Float,
                    vfx::ForceDefaults::TURBULENCE_FREQUENCY, 0.1f, 10.0f
                };
                node.properties["scrollSpeed"] = vfx::VFXProperty{
                    "scrollSpeed", vfx::VFXPropertyType::Float,
                    vfx::ForceDefaults::TURBULENCE_SCROLL_SPEED, 0.0f, 10.0f
                };
                node.properties["octaves"] = vfx::VFXProperty{
                    "octaves", vfx::VFXPropertyType::Int,
                    vfx::ForceDefaults::TURBULENCE_OCTAVES, 1.0f, 4.0f
                };
                node.properties["localSpace"] = vfx::VFXProperty{
                    "localSpace", vfx::VFXPropertyType::Bool,
                    false, 0.0f, 1.0f
                };
                break;

            case vfx::VFXNodeType::ForceVortex:
                node.properties["axis"] = vfx::VFXProperty{
                    "axis", vfx::VFXPropertyType::Vec3,
                    vfx::ForceDefaults::VORTEX_AXIS, -1.0f, 1.0f
                };
                node.properties["center"] = vfx::VFXProperty{
                    "center", vfx::VFXPropertyType::Vec3,
                    vfx::ForceDefaults::VORTEX_CENTER, -100.0f, 100.0f
                };
                node.properties["strength"] = vfx::VFXProperty{
                    "strength", vfx::VFXPropertyType::Float,
                    vfx::ForceDefaults::VORTEX_STRENGTH, 0.0f, 50.0f
                };
                node.properties["radialPull"] = vfx::VFXProperty{
                    "radialPull", vfx::VFXPropertyType::Float,
                    vfx::ForceDefaults::VORTEX_RADIAL_PULL, -50.0f, 50.0f
                };
                node.properties["localSpace"] = vfx::VFXProperty{
                    "localSpace", vfx::VFXPropertyType::Bool,
                    false, 0.0f, 1.0f
                };
                break;

            default:
                break;
        }
    }

    void VFXGraphEditor::handleContextMenu() {
        if (showContextMenu) {
            ImGui::OpenPopup("VFXContextMenu");
            showContextMenu = false;
        }

        if (ImGui::BeginPopup("VFXContextMenu")) {
            ImGui::TextDisabled("Add Node");
            ImGui::Separator();

            // VK-238: Modifiers submenu
            if (ImGui::BeginMenu("Modifiers")) {
                if (ImGui::MenuItem("Color Over Lifetime")) {
                    vfx::VFXNode newNode;
                    newNode.id = currentGraph->nextNodeId++;
                    newNode.type = vfx::VFXNodeType::ColorOverLifetime;
                    newNode.name = getNodeTypeName(vfx::VFXNodeType::ColorOverLifetime);
                    newNode.position = glm::vec2(contextMenuPosition.x, contextMenuPosition.y);
                    initializeModifierProperties(newNode);
                    currentGraph->nodes.push_back(std::move(newNode));
                    if (onGraphChanged) onGraphChanged();
                }
                if (ImGui::MenuItem("Size Over Lifetime")) {
                    vfx::VFXNode newNode;
                    newNode.id = currentGraph->nextNodeId++;
                    newNode.type = vfx::VFXNodeType::SizeOverLifetime;
                    newNode.name = getNodeTypeName(vfx::VFXNodeType::SizeOverLifetime);
                    newNode.position = glm::vec2(contextMenuPosition.x, contextMenuPosition.y);
                    initializeModifierProperties(newNode);
                    currentGraph->nodes.push_back(std::move(newNode));
                    if (onGraphChanged) onGraphChanged();
                }
                if (ImGui::MenuItem("Speed Over Lifetime")) {
                    vfx::VFXNode newNode;
                    newNode.id = currentGraph->nextNodeId++;
                    newNode.type = vfx::VFXNodeType::SpeedOverLifetime;
                    newNode.name = getNodeTypeName(vfx::VFXNodeType::SpeedOverLifetime);
                    newNode.position = glm::vec2(contextMenuPosition.x, contextMenuPosition.y);
                    initializeModifierProperties(newNode);
                    currentGraph->nodes.push_back(std::move(newNode));
                    if (onGraphChanged) onGraphChanged();
                }
                if (ImGui::MenuItem("Rotation Over Lifetime")) {
                    vfx::VFXNode newNode;
                    newNode.id = currentGraph->nextNodeId++;
                    newNode.type = vfx::VFXNodeType::RotationOverLifetime;
                    newNode.name = getNodeTypeName(vfx::VFXNodeType::RotationOverLifetime);
                    newNode.position = glm::vec2(contextMenuPosition.x, contextMenuPosition.y);
                    initializeModifierProperties(newNode);
                    currentGraph->nodes.push_back(std::move(newNode));
                    if (onGraphChanged) onGraphChanged();
                }
                ImGui::EndMenu();
            }

            // VK-239: Forces submenu
            if (ImGui::BeginMenu("Forces")) {
                if (ImGui::MenuItem("Gravity")) {
                    vfx::VFXNode newNode;
                    newNode.id = currentGraph->nextNodeId++;
                    newNode.type = vfx::VFXNodeType::ForceGravity;
                    newNode.name = getNodeTypeName(vfx::VFXNodeType::ForceGravity);
                    newNode.position = glm::vec2(contextMenuPosition.x, contextMenuPosition.y);
                    initializeForceProperties(newNode);
                    currentGraph->nodes.push_back(std::move(newNode));
                    if (onGraphChanged) onGraphChanged();
                }
                if (ImGui::MenuItem("Wind")) {
                    vfx::VFXNode newNode;
                    newNode.id = currentGraph->nextNodeId++;
                    newNode.type = vfx::VFXNodeType::ForceWind;
                    newNode.name = getNodeTypeName(vfx::VFXNodeType::ForceWind);
                    newNode.position = glm::vec2(contextMenuPosition.x, contextMenuPosition.y);
                    initializeForceProperties(newNode);
                    currentGraph->nodes.push_back(std::move(newNode));
                    if (onGraphChanged) onGraphChanged();
                }
                if (ImGui::MenuItem("Turbulence")) {
                    vfx::VFXNode newNode;
                    newNode.id = currentGraph->nextNodeId++;
                    newNode.type = vfx::VFXNodeType::ForceTurbulence;
                    newNode.name = getNodeTypeName(vfx::VFXNodeType::ForceTurbulence);
                    newNode.position = glm::vec2(contextMenuPosition.x, contextMenuPosition.y);
                    initializeForceProperties(newNode);
                    currentGraph->nodes.push_back(std::move(newNode));
                    if (onGraphChanged) onGraphChanged();
                }
                if (ImGui::MenuItem("Vortex")) {
                    vfx::VFXNode newNode;
                    newNode.id = currentGraph->nextNodeId++;
                    newNode.type = vfx::VFXNodeType::ForceVortex;
                    newNode.name = getNodeTypeName(vfx::VFXNodeType::ForceVortex);
                    newNode.position = glm::vec2(contextMenuPosition.x, contextMenuPosition.y);
                    initializeForceProperties(newNode);
                    currentGraph->nodes.push_back(std::move(newNode));
                    if (onGraphChanged) onGraphChanged();
                }
                ImGui::EndMenu();
            }

            ImGui::EndPopup();
        }
    }

}
