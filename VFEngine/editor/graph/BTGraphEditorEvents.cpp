#include "BTGraphEditor.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace ed = ax::NodeEditor;
using namespace behaviortree;

namespace editor::graph
{
    namespace
    {
        struct NodePaletteEntry
        {
            const char* category;
            const char* label;
            BTNodeType type;
        };

        constexpr std::array<NodePaletteEntry, 22> kNodePaletteEntries = {{
            // Composites
            {"Composites", "Sequence", BTNodeType::Sequence},
            {"Composites", "Selector", BTNodeType::Selector},
            {"Composites", "Parallel", BTNodeType::Parallel},
            // Decorators
            {"Decorators", "Inverter", BTNodeType::Inverter},
            {"Decorators", "Repeater", BTNodeType::Repeater},
            {"Decorators", "Succeeder", BTNodeType::Succeeder},
            {"Decorators", "Repeat Until Fail", BTNodeType::RepeatUntilFail},
            {"Decorators", "Cooldown", BTNodeType::Cooldown},
            {"Decorators", "Time Limit", BTNodeType::TimeLimit},
            {"Decorators", "Blackboard Condition", BTNodeType::BlackboardCondition},
            // Tasks
            {"Tasks", "Wait", BTNodeType::Wait},
            {"Tasks", "Log", BTNodeType::Log},
            {"Tasks", "Move To", BTNodeType::MoveTo},
            {"Tasks", "Play Animation", BTNodeType::PlayAnimation},
            {"Tasks", "Set Blackboard Value", BTNodeType::SetBlackboardValue},
            {"Tasks", "Check Blackboard Value", BTNodeType::CheckBlackboardValue},
            {"Tasks", "Script Task", BTNodeType::ScriptTask},
            {"Tasks", "Environment Query", BTNodeType::EnvironmentQuery},
            {"Tasks", "Line Of Sight", BTNodeType::LineOfSight},
            {"Tasks", "Run Subtree", BTNodeType::SubTree},
            {"Tasks", "Run Dynamic Subtree", BTNodeType::DynamicSubTree},
            // Services
            {"Services", "Service", BTNodeType::Service},
        }};

        bool containsCaseInsensitive(const char* text, const char* filter)
        {
            if (!filter || filter[0] == '\0')
            {
                return true;
            }

            if (!text)
            {
                return false;
            }

            auto toLower = [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            };

            std::string haystack(text);
            std::string needle(filter);
            std::transform(haystack.begin(), haystack.end(), haystack.begin(), toLower);
            std::transform(needle.begin(), needle.end(), needle.begin(), toLower);
            return haystack.find(needle) != std::string::npos;
        }

        bool matchesPaletteFilter(const NodePaletteEntry& entry, const char* filter)
        {
            return containsCaseInsensitive(entry.label, filter) ||
                   containsCaseInsensitive(entry.category, filter);
        }
    }

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
        static char nodePaletteFilter[128] = {};
        static bool focusNodePaletteFilter = false;

        auto createPaletteNode = [this](const NodePaletteEntry& entry)
        {
            createNode(entry.type, newNodePosition);
            nodePaletteFilter[0] = '\0';
            ImGui::CloseCurrentPopup();
        };

        ed::Suspend();

        ed::NodeId contextNodeId;
        if (ed::ShowBackgroundContextMenu())
        {
            nodePaletteFilter[0] = '\0';
            focusNodePaletteFilter = true;
            ImGui::OpenPopup("BTCreateNodeMenu");
            newNodePosition = ImGui::GetMousePos();
        }

        if (ImGui::BeginPopup("BTCreateNodeMenu"))
        {
            ImGui::TextDisabled("Add Node");

            if (focusNodePaletteFilter)
            {
                ImGui::SetKeyboardFocusHere();
                focusNodePaletteFilter = false;
            }
            ImGui::SetNextItemWidth(240.0f);
            bool searchSubmitted = ImGui::InputTextWithHint(
                "##BTNodePaletteSearch",
                "Search nodes...",
                nodePaletteFilter,
                sizeof(nodePaletteFilter),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

            bool hasFilter = nodePaletteFilter[0] != '\0';
            if (hasFilter && ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                nodePaletteFilter[0] = '\0';
                focusNodePaletteFilter = true;
                hasFilter = false;
            }

            const NodePaletteEntry* firstMatch = nullptr;
            if (hasFilter)
            {
                for (const auto& entry : kNodePaletteEntries)
                {
                    if (matchesPaletteFilter(entry, nodePaletteFilter))
                    {
                        firstMatch = &entry;
                        break;
                    }
                }
            }

            bool createdPaletteNode = false;
            if (hasFilter && searchSubmitted && firstMatch)
            {
                createPaletteNode(*firstMatch);
                createdPaletteNode = true;
            }

            ImGui::Separator();

            if (!createdPaletteNode && hasFilter)
            {
                bool anyMatch = false;
                for (const auto& entry : kNodePaletteEntries)
                {
                    if (!matchesPaletteFilter(entry, nodePaletteFilter))
                    {
                        continue;
                    }
                    anyMatch = true;
                    std::string menuLabel = (entry.category[0] != '\0')
                        ? std::string(entry.category) + " / " + entry.label
                        : std::string(entry.label);
                    if (ImGui::MenuItem(menuLabel.c_str()))
                    {
                        createPaletteNode(entry);
                        break;
                    }
                }
                if (!anyMatch)
                {
                    ImGui::TextDisabled("No matching nodes");
                }
            }
            else if (!createdPaletteNode)
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
                if (ImGui::MenuItem("Blackboard Condition")) createNode(BTNodeType::BlackboardCondition, newNodePosition);
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
                if (ImGui::MenuItem("Environment Query")) createNode(BTNodeType::EnvironmentQuery, newNodePosition);
                if (ImGui::MenuItem("Line Of Sight")) createNode(BTNodeType::LineOfSight, newNodePosition);
                if (ImGui::MenuItem("Run Subtree")) createNode(BTNodeType::SubTree, newNodePosition);
                if (ImGui::MenuItem("Run Dynamic Subtree")) createNode(BTNodeType::DynamicSubTree, newNodePosition);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Services"))
            {
                if (ImGui::MenuItem("Service")) createNode(BTNodeType::Service, newNodePosition);
                ImGui::EndMenu();
            }
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
        case BTNodeType::BlackboardCondition:
            node.properties["key"] = std::string("");
            node.properties["compareOp"] = std::string("==");
            node.properties["compareValue"] = 0.0f;
            node.properties["abortMode"] = std::string("None");
            break;
        case BTNodeType::EnvironmentQuery:
            node.properties["queryName"] = std::string("");
            node.properties["resultKey"] = std::string("eqsResult");
            break;
        case BTNodeType::SubTree:
            node.properties["treePath"] = std::string("");
            break;
        case BTNodeType::DynamicSubTree:
            node.properties["selectionKey"] = std::string("");
            node.properties["injectionTag"] = std::string("");
            node.properties["defaultTreePath"] = std::string("");
            break;
        case BTNodeType::LineOfSight:
            node.properties["targetKey"] = std::string("target");
            node.properties["maxDistance"] = 50.0f;
            node.properties["eyeOffset"] = 1.6f;
            break;
        case BTNodeType::Service:
            node.properties["interval"] = 0.5f;
            node.properties["randomDeviation"] = 0.0f;
            node.properties["runOnActivation"] = false;
            node.properties["serviceType"] = std::string("EQSRefresh");
            node.properties["queryName"] = std::string("");
            node.properties["resultKey"] = std::string("eqsResult");
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
