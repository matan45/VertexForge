#pragma once

#include <imgui.h>
#include <imgui_node_editor.h>
#include "../../utilities/behaviortree/BehaviorTreeTypes.hpp"
#include <functional>
#include <string>

namespace ax::NodeEditor
{
    struct EditorContext;
}

namespace editor::graph
{
    using BTGraphChangedCallback = std::function<void()>;

    class BTGraphEditor
    {
    private:
        ax::NodeEditor::EditorContext* editorContext = nullptr;
        behaviortree::BTGraph* currentGraph = nullptr;
        BTGraphChangedCallback onGraphChanged;
        uint32_t selectedNodeId = 0;
        bool needsPositionInit = false;
        ImVec2 newNodePosition;

        // Live per-node statuses while debugging a running tree (owned by the window, null when idle)
        const std::unordered_map<uint32_t, behaviortree::BTNodeStatus>* liveStatuses = nullptr;

        // ID offsets
        static constexpr uintptr_t NODE_ID_OFFSET = 100000;
        static constexpr uintptr_t PIN_ID_OFFSET = 200000;
        static constexpr uintptr_t LINK_ID_OFFSET = 300000;

    public:
        explicit BTGraphEditor();
        ~BTGraphEditor();

        void init();
        void cleanUp();

        void setGraph(behaviortree::BTGraph* graph);
        void draw();

        void setOnGraphChanged(BTGraphChangedCallback callback);
        uint32_t getSelectedNodeId() const { return selectedNodeId; }
        void navigateToContent();
        void setLiveStatus(const std::unordered_map<uint32_t, behaviortree::BTNodeStatus>* statuses)
        {
            liveStatuses = statuses;
        }

    private:
        ax::NodeEditor::NodeId toEditorNodeId(uint32_t nodeId) const;
        uint32_t fromEditorNodeId(ax::NodeEditor::NodeId edId) const;
        ax::NodeEditor::PinId toInputPinId(uint32_t nodeId) const;
        ax::NodeEditor::PinId toOutputPinId(uint32_t nodeId) const;
        ax::NodeEditor::LinkId toEditorLinkId(uint32_t linkId) const;
        uint32_t fromEditorLinkId(ax::NodeEditor::LinkId edId) const;
        bool isInputPin(ax::NodeEditor::PinId pinId) const;
        uint32_t nodeIdFromPin(ax::NodeEditor::PinId pinId) const;

        void initNodePositions();
        void drawNode(behaviortree::BTNode& node);
        void drawNodeInlineProperties(const behaviortree::BTNode& node);
        void drawLinks();

        void handleCreation();
        void handleDeletion();
        void handleContextMenu();
        void handleSelection();
        void createNode(behaviortree::BTNodeType type, const ImVec2& position);

        ImU32 getNodeColor(behaviortree::BTNodeType type) const;
        const char* getCategoryName(behaviortree::BTNodeType type) const;
        bool canCreateLink(uint32_t sourceNodeId, uint32_t targetNodeId) const;
        bool wouldCreateCycle(uint32_t sourceNodeId, uint32_t targetNodeId) const;
    };
}
