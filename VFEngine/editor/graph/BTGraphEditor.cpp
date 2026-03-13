#include "BTGraphEditor.hpp"
#include <algorithm>

namespace ed = ax::NodeEditor;

namespace editor::graph
{
    BTGraphEditor::BTGraphEditor() = default;

    BTGraphEditor::~BTGraphEditor()
    {
        cleanUp();
    }

    void BTGraphEditor::init()
    {
        ed::Config config;
        config.SettingsFile = nullptr;
        editorContext = ed::CreateEditor(&config);
    }

    void BTGraphEditor::cleanUp()
    {
        if (editorContext)
        {
            ed::DestroyEditor(editorContext);
            editorContext = nullptr;
        }
    }

    void BTGraphEditor::setGraph(behaviortree::BTGraph* graph)
    {
        currentGraph = graph;
        needsPositionInit = true;
        selectedNodeId = 0;
    }

    void BTGraphEditor::setOnGraphChanged(BTGraphChangedCallback callback)
    {
        onGraphChanged = std::move(callback);
    }

    void BTGraphEditor::navigateToContent()
    {
        if (editorContext)
        {
            ed::SetCurrentEditor(editorContext);
            ed::NavigateToContent();
            ed::SetCurrentEditor(nullptr);
        }
    }

    void BTGraphEditor::draw()
    {
        if (!editorContext || !currentGraph) return;

        ed::SetCurrentEditor(editorContext);

        auto canvasPos = ImGui::GetCursorScreenPos();
        auto canvasSize = ImGui::GetContentRegionAvail();

        ed::Begin("BTGraphEditor");

        if (needsPositionInit)
        {
            initNodePositions();
            needsPositionInit = false;
        }

        for (auto& node : currentGraph->nodes)
        {
            drawNode(node);
        }

        drawLinks();

        handleCreation();
        handleDeletion();
        handleContextMenu();

        for (auto& node : currentGraph->nodes)
        {
            ImVec2 pos = ed::GetNodePosition(toEditorNodeId(node.id));
            node.position = glm::vec2(pos.x, pos.y);
        }

        ed::End();

        handleSelection();

        ed::SetCurrentEditor(nullptr);
    }

    ed::NodeId BTGraphEditor::toEditorNodeId(uint32_t nodeId) const
    {
        return ed::NodeId(static_cast<uintptr_t>(nodeId) + NODE_ID_OFFSET);
    }

    uint32_t BTGraphEditor::fromEditorNodeId(ed::NodeId edId) const
    {
        return static_cast<uint32_t>(edId.Get() - NODE_ID_OFFSET);
    }

    ed::PinId BTGraphEditor::toInputPinId(uint32_t nodeId) const
    {
        return ed::PinId(static_cast<uintptr_t>(nodeId) * 2 + PIN_ID_OFFSET);
    }

    ed::PinId BTGraphEditor::toOutputPinId(uint32_t nodeId) const
    {
        return ed::PinId(static_cast<uintptr_t>(nodeId) * 2 + 1 + PIN_ID_OFFSET);
    }

    ed::LinkId BTGraphEditor::toEditorLinkId(uint32_t linkId) const
    {
        return ed::LinkId(static_cast<uintptr_t>(linkId) + LINK_ID_OFFSET);
    }

    uint32_t BTGraphEditor::fromEditorLinkId(ed::LinkId edId) const
    {
        return static_cast<uint32_t>(edId.Get() - LINK_ID_OFFSET);
    }

    bool BTGraphEditor::isInputPin(ed::PinId pinId) const
    {
        uintptr_t raw = pinId.Get() - PIN_ID_OFFSET;
        return (raw % 2) == 0;
    }

    uint32_t BTGraphEditor::nodeIdFromPin(ed::PinId pinId) const
    {
        uintptr_t raw = pinId.Get() - PIN_ID_OFFSET;
        return static_cast<uint32_t>(raw / 2);
    }

    void BTGraphEditor::initNodePositions()
    {
        for (const auto& node : currentGraph->nodes)
        {
            ed::SetNodePosition(toEditorNodeId(node.id),
                                ImVec2(node.position.x, node.position.y));
        }
        ed::NavigateToContent(0.0f);
    }

    ImU32 BTGraphEditor::getNodeColor(behaviortree::BTNodeType type) const
    {
        using namespace behaviortree;
        if (isRootNode(type)) return IM_COL32(80, 80, 80, 255);
        if (isCompositeNode(type)) return IM_COL32(50, 80, 140, 255);
        if (isDecoratorNode(type)) return IM_COL32(160, 100, 40, 255);
        if (isTaskNode(type)) return IM_COL32(50, 120, 60, 255);
        return IM_COL32(100, 100, 100, 255);
    }

    const char* BTGraphEditor::getCategoryName(behaviortree::BTNodeType type) const
    {
        using namespace behaviortree;
        if (isRootNode(type)) return "Root";
        if (isCompositeNode(type)) return "Composite";
        if (isDecoratorNode(type)) return "Decorator";
        if (isTaskNode(type)) return "Task";
        return "Unknown";
    }

    bool BTGraphEditor::canCreateLink(uint32_t sourceNodeId, uint32_t targetNodeId) const
    {
        if (sourceNodeId == targetNodeId) return false;

        const auto* sourceNode = currentGraph->findNodeById(sourceNodeId);
        const auto* targetNode = currentGraph->findNodeById(targetNodeId);
        if (!sourceNode || !targetNode) return false;

        // Source must have output pin (not a task/leaf)
        if (!behaviortree::hasOutputPin(sourceNode->type)) return false;

        // Target cannot be Root
        if (behaviortree::isRootNode(targetNode->type)) return false;

        // Target can only have one parent
        if (currentGraph->getParent(targetNodeId) != nullptr) return false;

        // Decorators can have only one child
        if (behaviortree::isDecoratorNode(sourceNode->type))
        {
            auto children = currentGraph->getChildren(sourceNodeId);
            if (!children.empty()) return false;
        }

        // Root can have only one child
        if (behaviortree::isRootNode(sourceNode->type))
        {
            auto children = currentGraph->getChildren(sourceNodeId);
            if (!children.empty()) return false;
        }

        // No cycles
        if (wouldCreateCycle(sourceNodeId, targetNodeId)) return false;

        return true;
    }

    bool BTGraphEditor::wouldCreateCycle(uint32_t sourceNodeId, uint32_t targetNodeId) const
    {
        // Check if targetNode is an ancestor of sourceNode
        // Walk up from sourceNode following parent links
        const behaviortree::BTNode* current = currentGraph->findNodeById(sourceNodeId);
        while (current)
        {
            if (current->id == targetNodeId) return true;
            current = currentGraph->getParent(current->id);
        }
        return false;
    }
}
