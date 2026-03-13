#include "BTGraphEditor.hpp"
#include <algorithm>

namespace ed = ax::NodeEditor;
using namespace behaviortree;

namespace editor::graph
{
    void BTGraphEditor::handleCreation()
    {
        if (ed::BeginCreate(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), 2.0f))
        {
            ed::PinId startPinId, endPinId;
            if (ed::QueryNewLink(&startPinId, &endPinId))
            {
                if (startPinId && endPinId && startPinId != endPinId)
                {
                    uint32_t sourceNodeId, targetNodeId;

                    if (isInputPin(startPinId))
                    {
                        targetNodeId = nodeIdFromPin(startPinId);
                        sourceNodeId = nodeIdFromPin(endPinId);
                    }
                    else
                    {
                        sourceNodeId = nodeIdFromPin(startPinId);
                        targetNodeId = nodeIdFromPin(endPinId);
                    }

                    if (canCreateLink(sourceNodeId, targetNodeId))
                    {
                        if (ed::AcceptNewItem(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), 3.0f))
                        {
                            uint32_t maxSort = 0;
                            for (const auto& link : currentGraph->links)
                            {
                                if (link.sourceNodeId == sourceNodeId)
                                {
                                    maxSort = std::max(maxSort, link.sortOrder + 1);
                                }
                            }

                            BTLink newLink;
                            newLink.id = currentGraph->nextLinkId++;
                            newLink.sourceNodeId = sourceNodeId;
                            newLink.targetNodeId = targetNodeId;
                            newLink.sortOrder = maxSort;
                            currentGraph->links.push_back(newLink);

                            if (onGraphChanged) onGraphChanged();
                        }
                    }
                    else
                    {
                        ed::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 2.0f);
                    }
                }
            }
        }
        ed::EndCreate();
    }

    void BTGraphEditor::handleDeletion()
    {
        if (ed::BeginDelete())
        {
            ed::LinkId deletedLinkId;
            while (ed::QueryDeletedLink(&deletedLinkId))
            {
                if (ed::AcceptDeletedItem())
                {
                    uint32_t linkId = fromEditorLinkId(deletedLinkId);
                    auto it = std::remove_if(currentGraph->links.begin(), currentGraph->links.end(),
                                             [linkId](const BTLink& l) { return l.id == linkId; });
                    currentGraph->links.erase(it, currentGraph->links.end());

                    if (onGraphChanged) onGraphChanged();
                }
            }

            ed::NodeId deletedNodeId;
            while (ed::QueryDeletedNode(&deletedNodeId))
            {
                uint32_t nodeId = fromEditorNodeId(deletedNodeId);
                const auto* node = currentGraph->findNodeById(nodeId);

                if (node && isRootNode(node->type))
                {
                    ed::RejectDeletedItem();
                    continue;
                }

                if (ed::AcceptDeletedItem())
                {
                    auto linkIt = std::remove_if(currentGraph->links.begin(), currentGraph->links.end(),
                                                  [nodeId](const BTLink& l)
                                                  {
                                                      return l.sourceNodeId == nodeId || l.targetNodeId == nodeId;
                                                  });
                    currentGraph->links.erase(linkIt, currentGraph->links.end());

                    auto nodeIt = std::remove_if(currentGraph->nodes.begin(), currentGraph->nodes.end(),
                                                  [nodeId](const BTNode& n) { return n.id == nodeId; });
                    currentGraph->nodes.erase(nodeIt, currentGraph->nodes.end());

                    if (selectedNodeId == nodeId) selectedNodeId = 0;

                    if (onGraphChanged) onGraphChanged();
                }
            }
        }
        ed::EndDelete();
    }

    void BTGraphEditor::handleSelection()
    {
        if (ed::HasSelectionChanged())
        {
            std::vector<ed::NodeId> selectedNodes;
            selectedNodes.resize(ed::GetSelectedObjectCount());
            int nodeCount = ed::GetSelectedNodes(selectedNodes.data(),
                                                  static_cast<int>(selectedNodes.size()));
            selectedNodes.resize(nodeCount);

            if (!selectedNodes.empty())
            {
                selectedNodeId = fromEditorNodeId(selectedNodes[0]);
            }
            else
            {
                selectedNodeId = 0;
            }
        }
    }

    void BTGraphEditor::handleContextMenu()
    {
        ed::Suspend();

        ed::NodeId contextNodeId;
        if (ed::ShowBackgroundContextMenu())
        {
            ImGui::OpenPopup("BTCreateNodeMenu");
            newNodePosition = ImGui::GetMousePos();
        }

        if (ImGui::BeginPopup("BTCreateNodeMenu"))
        {
            if (ImGui::BeginMenu("Composites"))
            {
                if (ImGui::MenuItem("Sequence")) createNode(BTNodeType::Sequence, newNodePosition);
                if (ImGui::MenuItem("Selector")) createNode(BTNodeType::Selector, newNodePosition);
                if (ImGui::MenuItem("Parallel")) createNode(BTNodeType::Parallel, newNodePosition);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Decorators"))
            {
                if (ImGui::MenuItem("Inverter")) createNode(BTNodeType::Inverter, newNodePosition);
                if (ImGui::MenuItem("Repeater")) createNode(BTNodeType::Repeater, newNodePosition);
                if (ImGui::MenuItem("Succeeder")) createNode(BTNodeType::Succeeder, newNodePosition);
                if (ImGui::MenuItem("Repeat Until Fail")) createNode(BTNodeType::RepeatUntilFail, newNodePosition);
                if (ImGui::MenuItem("Cooldown")) createNode(BTNodeType::Cooldown, newNodePosition);
                if (ImGui::MenuItem("Time Limit")) createNode(BTNodeType::TimeLimit, newNodePosition);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Tasks"))
            {
                if (ImGui::MenuItem("Wait")) createNode(BTNodeType::Wait, newNodePosition);
                if (ImGui::MenuItem("Log")) createNode(BTNodeType::Log, newNodePosition);
                if (ImGui::MenuItem("Move To")) createNode(BTNodeType::MoveTo, newNodePosition);
                if (ImGui::MenuItem("Play Animation")) createNode(BTNodeType::PlayAnimation, newNodePosition);
                if (ImGui::MenuItem("Set Blackboard Value")) createNode(BTNodeType::SetBlackboardValue, newNodePosition);
                if (ImGui::MenuItem("Check Blackboard Value")) createNode(BTNodeType::CheckBlackboardValue, newNodePosition);
                if (ImGui::MenuItem("Script Task")) createNode(BTNodeType::ScriptTask, newNodePosition);
                ImGui::EndMenu();
            }

            ImGui::EndPopup();
        }

        ed::Resume();
    }

    void BTGraphEditor::createNode(BTNodeType type, const ImVec2& position)
    {
        BTNode node;
        node.id = currentGraph->nextNodeId++;
        node.type = type;
        node.name = nodeTypeToString(type);
        node.position = glm::vec2(position.x, position.y);

        switch (type)
        {
        case BTNodeType::Wait:
            node.properties["duration"] = 1.0f;
            break;
        case BTNodeType::Log:
            node.properties["message"] = std::string("Hello");
            node.properties["level"] = std::string("Info");
            break;
        case BTNodeType::MoveTo:
            node.properties["targetKey"] = std::string("target");
            node.properties["arrivalDistance"] = 0.5f;
            break;
        case BTNodeType::PlayAnimation:
            node.properties["stateName"] = std::string("");
            node.properties["waitForCompletion"] = false;
            break;
        case BTNodeType::Parallel:
            node.properties["policy"] = std::string("RequireAll");
            break;
        case BTNodeType::Repeater:
            node.properties["repeatCount"] = 3;
            break;
        case BTNodeType::Cooldown:
            node.properties["cooldownTime"] = 1.0f;
            break;
        case BTNodeType::TimeLimit:
            node.properties["timeLimit"] = 5.0f;
            break;
        case BTNodeType::SetBlackboardValue:
            node.properties["key"] = std::string("");
            node.properties["value"] = 0.0f;
            break;
        case BTNodeType::CheckBlackboardValue:
            node.properties["key"] = std::string("");
            node.properties["compareOp"] = std::string("==");
            node.properties["compareValue"] = 0.0f;
            break;
        default:
            break;
        }

        currentGraph->nodes.push_back(std::move(node));

        ed::SetNodePosition(toEditorNodeId(currentGraph->nodes.back().id),
                            ImVec2(position.x, position.y));

        if (onGraphChanged) onGraphChanged();
    }
}
