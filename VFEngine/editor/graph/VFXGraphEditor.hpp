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
    private:
        ax::NodeEditor::EditorContext* editorContext = nullptr;
        vfx::VFXGraph* currentGraph = nullptr;
        VFXGraphChangedCallback onGraphChanged;

        uint32_t selectedNodeId = 0;

        // First frame flag - to initialize node positions
        bool needsPositionInit = false;

        bool showContextMenu = false;
        ImVec2 contextMenuPosition;

        int pendingZoomSteps = 0;

        // ID mapping helpers - use offsets to prevent conflicts between node/pin/link IDs
        static constexpr uintptr_t NODE_ID_OFFSET = 100000;
        static constexpr uintptr_t PIN_ID_OFFSET = 200000;
        static constexpr uintptr_t LINK_ID_OFFSET = 300000;
    public:
        explicit VFXGraphEditor();
        ~VFXGraphEditor();

        void init();
        void cleanUp();

        // Set the graph to edit (does not take ownership)
        void setGraph(vfx::VFXGraph* graph);

        void draw();

        void setOnGraphChanged(VFXGraphChangedCallback callback) { onGraphChanged = callback; }

        uint32_t getSelectedNodeId() const { return selectedNodeId; }

        void navigateToContent();

    private:

        ax::NodeEditor::NodeId toEditorNodeId(uint32_t id) const { return ax::NodeEditor::NodeId(NODE_ID_OFFSET + id); }
        ax::NodeEditor::PinId toEditorPinId(uint32_t id) const { return ax::NodeEditor::PinId(PIN_ID_OFFSET + id); }
        ax::NodeEditor::LinkId toEditorLinkId(uint32_t id) const { return ax::NodeEditor::LinkId(LINK_ID_OFFSET + id); }

        uint32_t fromEditorNodeId(ax::NodeEditor::NodeId id) const { return static_cast<uint32_t>(id.Get() - NODE_ID_OFFSET); }
        uint32_t fromEditorPinId(ax::NodeEditor::PinId id) const { return static_cast<uint32_t>(id.Get() - PIN_ID_OFFSET); }
        uint32_t fromEditorLinkId(ax::NodeEditor::LinkId id) const { return static_cast<uint32_t>(id.Get() - LINK_ID_OFFSET); }
        
        uint32_t getOutputPinId(uint32_t nodeId) const { return nodeId * 2; }
        uint32_t getInputPinId(uint32_t nodeId) const { return nodeId * 2 + 1; }
        uint32_t getNodeIdFromPinId(uint32_t pinId) const { return pinId / 2; }
        bool isOutputPin(uint32_t pinId) const { return (pinId % 2) == 0; }

        // VK-240: Shape pin ID helpers - use large offset to avoid conflicts
        static constexpr uint32_t SHAPE_PIN_OFFSET = 1000000;
        uint32_t getShapePinId(uint32_t nodeId) const { return SHAPE_PIN_OFFSET + nodeId; }
        bool isShapePin(uint32_t pinId) const { return pinId >= SHAPE_PIN_OFFSET; }
        uint32_t getNodeIdFromShapePinId(uint32_t pinId) const { return pinId - SHAPE_PIN_OFFSET; }

        void drawNode(vfx::VFXNode& node);
        void drawLinks();
        void drawZoomControls(ImVec2 canvasPos, ImVec2 canvasSize, float currentZoom);
        void drawFlowPinShape(ImDrawList* drawList, ImVec2 center, ImU32 color, bool filled, float size, bool isOutput) const;
        bool isPinLinked(uint32_t pinId) const;

        void handleCreation();
        void handleDeletion();
        void handleContextMenu();

        bool canCreateLink(uint32_t startPinId, uint32_t endPinId) const;
        vfx::VFXNode* findNodeByPinId(uint32_t pinId);
        const vfx::VFXNode* findNodeByPinId(uint32_t pinId) const;
        ImU32 getNodeHeaderColor(vfx::VFXNodeType type) const;
        const char* getNodeTypeName(vfx::VFXNodeType type) const;
        ImU32 getFlowPinColor() const;
    };

}
