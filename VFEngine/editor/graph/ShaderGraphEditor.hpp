#pragma once

#include <material/MaterialTypes.hpp>
#include <imgui_node_editor.h>
#include <memory>
#include <string>
#include <functional>
#include <map>

// Forward declarations
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

        // Initialize the editor context
        void init();

        // Clean up resources
        void cleanUp();

        // Set the graph to edit (does not take ownership)
        void setGraph(material::ShaderGraph* graph);

        // Draw the editor (call within ImGui window)
        void draw();

        // Register callback for graph changes
        void setOnGraphChanged(GraphChangedCallback callback) { onGraphChanged = callback; }

        // Get selected node ID (0 if none)
        uint32_t getSelectedNodeId() const { return selectedNodeId; }

        // Center view on all nodes
        void navigateToContent();

    private:
        ax::NodeEditor::EditorContext* editorContext = nullptr;
        material::ShaderGraph* currentGraph = nullptr;
        GraphChangedCallback onGraphChanged;

        // Selection state
        uint32_t selectedNodeId = 0;

        // First frame flag - to initialize node positions
        bool needsPositionInit = false;

        // Context menu state
        bool showCreateNodeMenu = false;
        ImVec2 newNodePosition;
        ImVec2 popupMousePos;  // Mouse position for popup placement
        ax::NodeEditor::PinId newNodeLinkPin;

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

        // Drawing helpers
        void drawNode(material::ShaderNode& node);
        void drawLinks();

        // Handle interactions
        void handleCreation();
        void handleDeletion();
        void handleContextMenu();

        // Create node of given type
        void createNode(material::NodeType type, const ImVec2& position);

        // Find pin in graph
        const material::NodePin* findPin(uint32_t pinId) const;
        material::ShaderNode* findNodeByPinId(uint32_t pinId);

        // Check if link is valid (type compatibility)
        bool canCreateLink(uint32_t startPinId, uint32_t endPinId) const;

        // Get color for pin type
        ImU32 getPinColor(material::PinType type) const;

        // Get node header color
        ImU32 getNodeHeaderColor(material::NodeType type) const;

        // Get display name for node type
        const char* getNodeTypeName(material::NodeType type) const;

        // Check if a pin has a link connected
        bool isPinLinked(uint32_t pinId) const;

        // Draw pin shape based on type (filled if linked, hollow if not)
        void drawPinShape(ImDrawList* drawList, ImVec2 center, material::PinType type,
                          ImU32 color, bool filled, float size) const;
    };

}
