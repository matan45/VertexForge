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

        if (startIsOutput == endIsOutput) return false;

        const vfx::VFXNode* sourceNode = startIsOutput ? startNode : endNode;
        const vfx::VFXNode* targetNode = startIsOutput ? endNode : startNode;

        if (sourceNode->type != vfx::VFXNodeType::Emitter) return false;
        if (targetNode->type != vfx::VFXNodeType::OutSystem) return false;

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

    void VFXGraphEditor::handleContextMenu() {
        if (showContextMenu) {
            ImGui::OpenPopup("VFXContextMenu");
            showContextMenu = false;
        }

        if (ImGui::BeginPopup("VFXContextMenu")) {
            // Part 1: No new nodes can be added from context menu
            ImGui::TextDisabled("No nodes available");
            ImGui::Separator();
            ImGui::TextDisabled("(Part 1 - Emitter and Output only)");

            ImGui::EndPopup();
        }
    }

}
