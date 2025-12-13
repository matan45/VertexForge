#pragma once

#include <material/MaterialTypes.hpp>
#include <imgui_node_editor.h>
#include <memory>
#include <string>
#include <functional>
#include <map>

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

        // Context menu state
        bool showCreateNodeMenu = false;
        ImVec2 newNodePosition;
        ax::NodeEditor::PinId newNodeLinkPin;

        // ID mapping helpers
        ax::NodeEditor::NodeId toEditorNodeId(uint32_t id) const { return ax::NodeEditor::NodeId(id); }
        ax::NodeEditor::PinId toEditorPinId(uint32_t id) const { return ax::NodeEditor::PinId(id); }
        ax::NodeEditor::LinkId toEditorLinkId(uint32_t id) const { return ax::NodeEditor::LinkId(id); }

        uint32_t fromEditorNodeId(ax::NodeEditor::NodeId id) const { return static_cast<uint32_t>(id.Get()); }
        uint32_t fromEditorPinId(ax::NodeEditor::PinId id) const { return static_cast<uint32_t>(id.Get()); }
        uint32_t fromEditorLinkId(ax::NodeEditor::LinkId id) const { return static_cast<uint32_t>(id.Get()); }

        // Drawing helpers
        void drawNode(material::ShaderNode& node);
        void drawPin(const material::NodePin& pin, bool isOutput);
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
    };

}
