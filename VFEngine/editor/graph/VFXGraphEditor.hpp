#pragma once

#include <vfx/VFXTypes.hpp>
#include <imgui_node_editor.h>
#include <functional>
#include <string>

struct ImDrawList;

namespace ax::NodeEditor {
    struct EditorContext;
}

namespace editor::graph {

    // Callback when graph is modified
    using VFXGraphChangedCallback = std::function<void()>;

    class VFXGraphEditor {
    public:
        VFXGraphEditor();
        ~VFXGraphEditor();

        void init();
        void cleanUp();

        // Set the graph to edit (does not take ownership)
        void setGraph(vfx::VFXGraph* graph);

        void draw();

        // Register callback for graph changes
        void setOnGraphChanged(VFXGraphChangedCallback callback) { onGraphChanged = callback; }

        uint32_t getSelectedNodeId() const { return selectedNodeId; }

        // Center view on all nodes
        void navigateToContent();

    private:
        ax::NodeEditor::EditorContext* editorContext = nullptr;
        vfx::VFXGraph* currentGraph = nullptr;
        VFXGraphChangedCallback onGraphChanged;

        uint32_t selectedNodeId = 0;

        // First frame flag - to initialize node positions
        bool needsPositionInit = false;

        // Context menu state
        bool showContextMenu = false;
        ImVec2 contextMenuPosition;

        int pendingZoomSteps = 0;

        // ID mapping helpers - use offsets to prevent conflicts between node/pin/link IDs
        static constexpr uintptr_t NODE_ID_OFFSET = 100000;
        static constexpr uintptr_t PIN_ID_OFFSET = 200000;
        static constexpr uintptr_t LINK_ID_OFFSET = 300000;

        ax::NodeEditor::NodeId toEditorNodeId(uint32_t id) const { return ax::NodeEditor::NodeId(NODE_ID_OFFSET + id); }
        ax::NodeEditor::PinId toEditorPinId(uint32_t id) const { return ax::NodeEditor::PinId(PIN_ID_OFFSET + id); }
        ax::NodeEditor::LinkId toEditorLinkId(uint32_t id) const { return ax::NodeEditor::LinkId(LINK_ID_OFFSET + id); }

        uint32_t fromEditorNodeId(ax::NodeEditor::NodeId id) const { return static_cast<uint32_t>(id.Get() - NODE_ID_OFFSET); }
        uint32_t fromEditorPinId(ax::NodeEditor::PinId id) const { return static_cast<uint32_t>(id.Get() - PIN_ID_OFFSET); }
        uint32_t fromEditorLinkId(ax::NodeEditor::LinkId id) const { return static_cast<uint32_t>(id.Get() - LINK_ID_OFFSET); }

        // Pin ID generation for VFX nodes
        // Each node has fixed pins: Emitter has Output (pin 0), OutSystem has Input (pin 1)
        // We use nodeId * 2 for output pins, nodeId * 2 + 1 for input pins
        uint32_t getOutputPinId(uint32_t nodeId) const { return nodeId * 2; }
        uint32_t getInputPinId(uint32_t nodeId) const { return nodeId * 2 + 1; }
        uint32_t getNodeIdFromPinId(uint32_t pinId) const { return pinId / 2; }
        bool isOutputPin(uint32_t pinId) const { return (pinId % 2) == 0; }

        // Drawing methods (in VFXGraphEditorDraw.cpp)
        void drawNode(vfx::VFXNode& node);
        void drawLinks();
        void drawZoomControls(ImVec2 canvasPos, ImVec2 canvasSize, float currentZoom);
        void drawFlowPinShape(ImDrawList* drawList, ImVec2 center, ImU32 color, bool filled, float size, bool isOutput) const;
        bool isPinLinked(uint32_t pinId) const;

        // Event handling (in VFXGraphEditorEvents.cpp)
        void handleCreation();
        void handleDeletion();
        void handleContextMenu();

        // Utility functions
        bool canCreateLink(uint32_t startPinId, uint32_t endPinId) const;
        vfx::VFXNode* findNodeByPinId(uint32_t pinId);
        const vfx::VFXNode* findNodeByPinId(uint32_t pinId) const;
        ImU32 getNodeHeaderColor(vfx::VFXNodeType type) const;
        const char* getNodeTypeName(vfx::VFXNodeType type) const;
        ImU32 getFlowPinColor() const;
    };

}
