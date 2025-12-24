#pragma once

#include <material/MaterialTypes.hpp>
#include <imgui_node_editor.h>
#include <memory>
#include <string>
#include <string_view>
#include <functional>
#include <map>

struct ImDrawList;

namespace ax::NodeEditor {
    struct EditorContext;
}

namespace editor::graph {

    // Callback when graph is modified
    using GraphChangedCallback = std::function<void()>;

    class ShaderGraphEditor {
    public:
        ShaderGraphEditor();
        ~ShaderGraphEditor();
        
        void init();
        
        void cleanUp();

        // Set the graph to edit (does not take ownership)
        void setGraph(material::ShaderGraph* graph);
        
        void draw();

        // Register callback for graph changes
        void setOnGraphChanged(GraphChangedCallback callback) { onGraphChanged = callback; }
        
        uint32_t getSelectedNodeId() const { return selectedNodeId; }

        // Center view on all nodes
        void navigateToContent();

    private:
        ax::NodeEditor::EditorContext* editorContext = nullptr;
        material::ShaderGraph* currentGraph = nullptr;
        GraphChangedCallback onGraphChanged;
        
        uint32_t selectedNodeId = 0;

        // First frame flag - to initialize node positions
        bool needsPositionInit = false;

        // Context menu state
        bool showCreateNodeMenu = false;
        ImVec2 newNodePosition;
        ImVec2 popupMousePos; 
        ax::NodeEditor::PinId newNodeLinkPin;
        
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
        
        void drawNode(material::ShaderNode& node);
        void drawLinks();
        void drawZoomControls(ImVec2 canvasPos, ImVec2 canvasSize, float currentZoom);
        void drawPinShape(ImDrawList* drawList, ImVec2 center, material::PinType type,
                          ImU32 color, bool filled, float size) const;
        bool isPinLinked(uint32_t pinId) const;

        // Event handling (in ShaderGraphEditorEvents.cpp)
        void handleCreation();
        void handleDeletion();
        void handleContextMenu();
        void createNode(material::NodeType type, const ImVec2& position);

        // Utility functions (in ShaderGraphEditorUtils.cpp)
        const material::NodePin* findPin(uint32_t pinId) const;
        material::ShaderNode* findNodeByPinId(uint32_t pinId);
        bool canCreateLink(uint32_t startPinId, uint32_t endPinId) const;
        std::string getTypeMismatchMessage(uint32_t startPinId, uint32_t endPinId) const;
        std::string getConversionNodeName(material::PinType srcType, material::PinType dstType) const;
        ImU32 getPinColor(material::PinType type) const;
        ImU32 getNodeHeaderColor(material::NodeType type) const;
        std::string_view getNodeTypeName(material::NodeType type) const;
    };

}
