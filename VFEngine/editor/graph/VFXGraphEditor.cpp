#include "VFXGraphEditor.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace ed = ax::NodeEditor;

namespace editor::graph {

    VFXGraphEditor::VFXGraphEditor() = default;

    VFXGraphEditor::~VFXGraphEditor() {
        cleanUp();
    }

    void VFXGraphEditor::init() {
        if (editorContext) return;

        ed::Config config;
        config.SettingsFile = nullptr;
        config.NavigateButtonIndex = 1;
        editorContext = ed::CreateEditor(&config);
    }

    void VFXGraphEditor::cleanUp() {
        if (editorContext) {
            ed::DestroyEditor(editorContext);
            editorContext = nullptr;
        }
    }

    void VFXGraphEditor::setGraph(vfx::VFXGraph* graph) {
        currentGraph = graph;
        needsPositionInit = true;
    }

    void VFXGraphEditor::navigateToContent() {
        if (editorContext) {
            ed::SetCurrentEditor(editorContext);
            ed::NavigateToContent();
            ed::SetCurrentEditor(nullptr);
        }
    }

    void VFXGraphEditor::draw() {
        if (!editorContext || !currentGraph) {
            ImGui::TextDisabled("No VFX loaded");
            return;
        }

        ed::SetCurrentEditor(editorContext);

        // Store canvas info for zoom controls
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();

        ed::Begin("VFXGraphEditor");

        // Get current zoom level
        float currentZoom = ed::GetCurrentZoom();

        // Apply pending zoom
        if (pendingZoomSteps != 0) {
            float zoomStep = 0.1f;
            float newZoom;
            if (pendingZoomSteps > 0) {
                newZoom = currentZoom + zoomStep;
            } else {
                newZoom = currentZoom - zoomStep;
            }
            newZoom = std::round(newZoom * 10.0f) / 10.0f;
            newZoom = std::clamp(newZoom, 0.1f, 5.0f);
            ed::SetCurrentZoom(newZoom);
            currentZoom = ed::GetCurrentZoom();
            pendingZoomSteps = 0;
        }

        // Initialize node positions on first frame
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

        // Check for context menu trigger
        ed::Suspend();
        if (ed::ShowBackgroundContextMenu()) {
            contextMenuPosition = ed::ScreenToCanvas(ImGui::GetMousePos());
            showContextMenu = true;
        }
        ed::Resume();

        ed::End();

        // Draw zoom controls overlay
        drawZoomControls(canvasPos, canvasSize, currentZoom);

        // Update selection
        if (ed::HasSelectionChanged()) {
            ed::NodeId selectedNodes[1];
            int count = ed::GetSelectedNodes(selectedNodes, 1);
            selectedNodeId = count > 0 ? fromEditorNodeId(selectedNodes[0]) : 0;
        }

        // Update node positions in graph data
        for (auto& node : currentGraph->nodes) {
            ImVec2 pos = ed::GetNodePosition(toEditorNodeId(node.id));
            if (pos.x != node.position.x || pos.y != node.position.y) {
                node.position.x = pos.x;
                node.position.y = pos.y;
            }
        }

        ed::SetCurrentEditor(nullptr);

        // Handle popup outside of editor context
        handleContextMenu();
    }

    // Utility functions
    vfx::VFXNode* VFXGraphEditor::findNodeByPinId(uint32_t pinId) {
        uint32_t nodeId = getNodeIdFromPinId(pinId);
        return currentGraph->findNode(nodeId);
    }

    const vfx::VFXNode* VFXGraphEditor::findNodeByPinId(uint32_t pinId) const {
        uint32_t nodeId = getNodeIdFromPinId(pinId);
        return currentGraph->findNode(nodeId);
    }

    ImU32 VFXGraphEditor::getNodeHeaderColor(vfx::VFXNodeType type) const {
        switch (type) {
            case vfx::VFXNodeType::Emitter:
                return IM_COL32(100, 180, 100, 255);  // Green - Start node
            case vfx::VFXNodeType::OutSystem:
                return IM_COL32(180, 100, 100, 255);  // Red - End node
            default:
                return IM_COL32(100, 100, 100, 255);
        }
    }

    const char* VFXGraphEditor::getNodeTypeName(vfx::VFXNodeType type) const {
        switch (type) {
            case vfx::VFXNodeType::Emitter:   return "Emitter";
            case vfx::VFXNodeType::OutSystem: return "Output";
            default:                          return "Unknown";
        }
    }

    ImU32 VFXGraphEditor::getFlowPinColor() const {
        return IM_COL32(200, 200, 200, 255);  // White/gray for flow connections
    }

}
