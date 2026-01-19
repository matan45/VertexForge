#include "AnimatorNodeGraph.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace ed = ax::NodeEditor;
namespace fs = std::filesystem;

namespace windows::animation
{
    void AnimatorNodeGraph::init()
    {
        ed::Config config;
        config.SettingsFile = nullptr;
        config.NavigateButtonIndex = 1;
        nodeEditorContext = ed::CreateEditor(&config);
    }

    void AnimatorNodeGraph::cleanUp()
    {
        if (nodeEditorContext)
        {
            ed::DestroyEditor(nodeEditorContext);
            nodeEditorContext = nullptr;
        }
    }

    void AnimatorNodeGraph::draw(animator::AnimatorData* animatorData,
                                  uint32_t& selectedStateId,
                                  uint32_t& selectedTransitionId,
                                  bool& isDirty,
                                  bool& needsPositionInit,
                                  bool& needsNavigateToContent,
                                  int& pendingZoomSteps)
    {
        if (!animatorData || !nodeEditorContext)
            return;

        ed::SetCurrentEditor(nodeEditorContext);

        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();

        ImGui::Separator();

        ed::Begin("AnimatorGraph");

        float currentZoom = ed::GetCurrentZoom();

        if (pendingZoomSteps != 0)
        {
            float zoomStep = 0.1f;
            float newZoom;
            if (pendingZoomSteps > 0)
            {
                newZoom = currentZoom + zoomStep;
            }
            else
            {
                newZoom = currentZoom - zoomStep;
            }
            newZoom = std::round(newZoom * 10.0f) / 10.0f;
            newZoom = std::clamp(newZoom, 0.1f, 5.0f);
            ed::SetCurrentZoom(newZoom);
            currentZoom = ed::GetCurrentZoom();
            pendingZoomSteps = 0;
        }

        if (needsPositionInit)
        {
            ed::SetNodePosition(specialNodeId(ENTRY_NODE),
                                ImVec2(animatorData->graph.entryPosition.x, animatorData->graph.entryPosition.y));
            ed::SetNodePosition(specialNodeId(ANY_STATE_NODE),
                                ImVec2(animatorData->graph.anyStatePosition.x, animatorData->graph.anyStatePosition.y));

            for (const auto& state : animatorData->graph.states)
            {
                ed::SetNodePosition(stateIdToNodeId(state.id),
                                    ImVec2(state.position.x, state.position.y));
            }

            needsPositionInit = false;

            if (needsNavigateToContent)
            {
                ed::NavigateToContent();
                needsNavigateToContent = false;
            }
        }

        drawSpecialNodes();

        for (const auto& state : animatorData->graph.states)
        {
            drawStateNode(state, animatorData->graph.defaultStateId);
        }

        drawTransitionLinks(animatorData);

        handleNodeCreation(animatorData, isDirty);
        handleDeletion(animatorData, selectedStateId, selectedTransitionId, isDirty);

        updateSelection(selectedStateId, selectedTransitionId);

        syncNodePositions(animatorData, isDirty);

        ed::End();

        drawZoomControls(canvasPos, canvasSize, currentZoom, pendingZoomSteps);

        ed::SetCurrentEditor(nullptr);
    }

    void AnimatorNodeGraph::drawSpecialNodes()
    {
        ed::NodeId entryNodeId = specialNodeId(ENTRY_NODE);
        ed::PinId entryPinId = specialPinId(ENTRY_NODE);

        ed::BeginNode(entryNodeId);
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "Entry");
        ed::BeginPin(entryPinId, ed::PinKind::Output);
        ImGui::Text("->");
        ed::EndPin();
        ed::EndNode();

        ed::NodeId anyStateNodeId = specialNodeId(ANY_STATE_NODE);
        ed::PinId anyStatePinId = specialPinId(ANY_STATE_NODE);

        ed::BeginNode(anyStateNodeId);
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1.0f), "Any State");
        ed::BeginPin(anyStatePinId, ed::PinKind::Output);
        ImGui::Text("->");
        ed::EndPin();
        ed::EndNode();
    }

    void AnimatorNodeGraph::drawStateNode(const animator::AnimatorState& state, uint32_t defaultStateId)
    {
        ed::NodeId nodeId = stateIdToNodeId(state.id);
        ed::PinId inputPinId = statePinId(state.id, true);
        ed::PinId outputPinId = statePinId(state.id, false);

        bool isDefault = (state.id == defaultStateId);

        const ImVec4& nodeColor = isDefault ? DEFAULT_STATE_COLOR : STATE_NODE_COLOR;
        ed::PushStyleColor(ed::StyleColor_NodeBg, nodeColor);

        ed::BeginNode(nodeId);

        ed::BeginPin(inputPinId, ed::PinKind::Input);
        ImGui::Text("->");
        ed::EndPin();

        ImGui::SameLine();

        ImGui::BeginGroup();
        ImGui::Text("%s", state.name.c_str());
        if (isDefault)
        {
            ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "(Default)");
        }
        if (!state.animationPath.empty())
        {
            fs::path animPath(state.animationPath);
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", animPath.filename().string().c_str());
        }
        ImGui::EndGroup();

        ImGui::SameLine();

        ed::BeginPin(outputPinId, ed::PinKind::Output);
        ImGui::Text("->");
        ed::EndPin();

        ed::EndNode();
        ed::PopStyleColor();
    }

    void AnimatorNodeGraph::drawTransitionLinks(animator::AnimatorData* animatorData)
    {
        for (const auto& transition : animatorData->graph.transitions)
        {
            ed::LinkId linkId = transitionIdToLinkId(transition.id);

            ed::PinId startPin;
            if (transition.sourceStateId == 0)
            {
                startPin = specialPinId(ANY_STATE_NODE);
            }
            else
            {
                startPin = statePinId(transition.sourceStateId, false);
            }

            ed::PinId endPin = statePinId(transition.targetStateId, true);

            ImVec4 linkColor = transition.conditions.empty()
                                   ? ImVec4(0.8f, 0.8f, 0.8f, 1.0f)
                                   : ImVec4(0.4f, 0.8f, 1.0f, 1.0f);

            ed::Link(linkId, startPin, endPin, linkColor, 2.0f);
        }

        if (animatorData->graph.defaultStateId != 0)
        {
            ed::PinId entryPin = specialPinId(ENTRY_NODE);
            ed::PinId defaultStatePin = statePinId(animatorData->graph.defaultStateId, true);
            ed::Link(ed::LinkId(LINK_OFFSET), entryPin, defaultStatePin, ImVec4(0.3f, 0.9f, 0.3f, 1.0f), 3.0f);
        }
    }

    void AnimatorNodeGraph::handleNodeCreation(animator::AnimatorData* animatorData, bool& isDirty)
    {
        if (ed::BeginCreate())
        {
            ed::PinId startPinId, endPinId;
            if (ed::QueryNewLink(&startPinId, &endPinId))
            {
                if (startPinId && endPinId && ed::AcceptNewItem())
                {
                    uint32_t sourceStateId = 0;
                    uint32_t targetStateId = 0;

                    uintptr_t startId = startPinId.Get();
                    uintptr_t endId = endPinId.Get();

                    if (startId == SPECIAL_PIN_OFFSET + ANY_STATE_NODE)
                    {
                        sourceStateId = 0;
                    }
                    else if (startId >= OUTPUT_PIN_OFFSET)
                    {
                        sourceStateId = static_cast<uint32_t>(startId - OUTPUT_PIN_OFFSET);
                    }

                    if (endId >= INPUT_PIN_OFFSET && endId < OUTPUT_PIN_OFFSET)
                    {
                        targetStateId = static_cast<uint32_t>(endId - INPUT_PIN_OFFSET);
                    }

                    if (targetStateId != 0)
                    {
                        animator::AnimatorTransition transition;
                        transition.id = animatorData->graph.nextTransitionId++;
                        transition.sourceStateId = sourceStateId;
                        transition.targetStateId = targetStateId;
                        animatorData->graph.transitions.push_back(std::move(transition));
                        isDirty = true;
                    }
                }
            }
        }
        ed::EndCreate();
    }

    void AnimatorNodeGraph::handleDeletion(animator::AnimatorData* animatorData,
                                            uint32_t& selectedStateId,
                                            uint32_t& selectedTransitionId,
                                            bool& isDirty)
    {
        if (ed::BeginDelete())
        {
            ed::NodeId nodeId;
            while (ed::QueryDeletedNode(&nodeId))
            {
                if (ed::AcceptDeletedItem())
                {
                    uint32_t stateId = nodeIdToStateId(nodeId);
                    if (stateId != 0 && stateId != animatorData->graph.defaultStateId)
                    {
                        auto& states = animatorData->graph.states;
                        states.erase(
                            std::remove_if(states.begin(), states.end(),
                                           [stateId](const animator::AnimatorState& s)
                                           {
                                               return s.id == stateId;
                                           }),
                            states.end());

                        auto& transitions = animatorData->graph.transitions;
                        transitions.erase(
                            std::remove_if(transitions.begin(), transitions.end(),
                                           [stateId](const animator::AnimatorTransition& t)
                                           {
                                               return t.sourceStateId == stateId ||
                                                   t.targetStateId == stateId;
                                           }),
                            transitions.end());

                        if (selectedStateId == stateId)
                            selectedStateId = 0;

                        isDirty = true;
                    }
                }
            }

            ed::LinkId linkId;
            while (ed::QueryDeletedLink(&linkId))
            {
                if (ed::AcceptDeletedItem())
                {
                    uint32_t transitionId = linkIdToTransitionId(linkId);
                    if (transitionId != 0)
                    {
                        auto& transitions = animatorData->graph.transitions;
                        transitions.erase(
                            std::remove_if(transitions.begin(), transitions.end(),
                                           [transitionId](const animator::AnimatorTransition& t)
                                           {
                                               return t.id == transitionId;
                                           }),
                            transitions.end());

                        if (selectedTransitionId == transitionId)
                            selectedTransitionId = 0;

                        isDirty = true;
                    }
                }
            }
        }
        ed::EndDelete();
    }

    void AnimatorNodeGraph::syncNodePositions(animator::AnimatorData* animatorData, bool& isDirty)
    {
        for (auto& state : animatorData->graph.states)
        {
            ed::NodeId nodeId = stateIdToNodeId(state.id);
            ImVec2 pos = ed::GetNodePosition(nodeId);
            if (pos.x != state.position.x || pos.y != state.position.y)
            {
                state.position.x = pos.x;
                state.position.y = pos.y;
                isDirty = true;
            }
        }

        ImVec2 entryPos = ed::GetNodePosition(specialNodeId(ENTRY_NODE));
        if (entryPos.x != animatorData->graph.entryPosition.x ||
            entryPos.y != animatorData->graph.entryPosition.y)
        {
            animatorData->graph.entryPosition.x = entryPos.x;
            animatorData->graph.entryPosition.y = entryPos.y;
            isDirty = true;
        }

        ImVec2 anyStatePos = ed::GetNodePosition(specialNodeId(ANY_STATE_NODE));
        if (anyStatePos.x != animatorData->graph.anyStatePosition.x ||
            anyStatePos.y != animatorData->graph.anyStatePosition.y)
        {
            animatorData->graph.anyStatePosition.x = anyStatePos.x;
            animatorData->graph.anyStatePosition.y = anyStatePos.y;
            isDirty = true;
        }
    }

    void AnimatorNodeGraph::updateSelection(uint32_t& selectedStateId, uint32_t& selectedTransitionId)
    {
        if (ed::GetSelectedObjectCount() > 0)
        {
            std::vector<ed::NodeId> selectedNodes(ed::GetSelectedObjectCount());
            int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), static_cast<int>(selectedNodes.size()));

            if (nodeCount > 0)
            {
                uint32_t stateId = nodeIdToStateId(selectedNodes[0]);
                if (stateId != 0)
                {
                    selectedStateId = stateId;
                    selectedTransitionId = 0;
                }
            }

            std::vector<ed::LinkId> selectedLinks(ed::GetSelectedObjectCount());
            int linkCount = ed::GetSelectedLinks(selectedLinks.data(), static_cast<int>(selectedLinks.size()));

            if (linkCount > 0)
            {
                uint32_t transitionId = linkIdToTransitionId(selectedLinks[0]);
                if (transitionId != 0)
                {
                    selectedTransitionId = transitionId;
                }
            }
        }
    }

    void AnimatorNodeGraph::drawZoomControls(ImVec2 canvasPos, ImVec2 canvasSize, float currentZoom, int& pendingZoomSteps)
    {
        ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();

        float panelX = canvasPos.x + canvasSize.x - 130;
        float panelY = canvasPos.y + canvasSize.y - 40;
        float panelWidth = 125;
        float panelHeight = 35;

        fgDrawList->AddRectFilled(
            ImVec2(panelX, panelY),
            ImVec2(panelX + panelWidth, panelY + panelHeight),
            IM_COL32(30, 30, 30, 220), 6.0f);
        fgDrawList->AddRect(
            ImVec2(panelX, panelY),
            ImVec2(panelX + panelWidth, panelY + panelHeight),
            IM_COL32(60, 60, 60, 255), 6.0f);

        float btnSize = 25;
        float btnY = panelY + 5;
        float btnSpacing = 5;

        ImVec2 mousePos = ImGui::GetMousePos();
        bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

        float zoomOutX = panelX + 5;
        ImVec2 zoomOutMin(zoomOutX, btnY);
        ImVec2 zoomOutMax(zoomOutX + btnSize, btnY + btnSize);
        bool zoomOutHovered = mousePos.x >= zoomOutMin.x && mousePos.x <= zoomOutMax.x &&
            mousePos.y >= zoomOutMin.y && mousePos.y <= zoomOutMax.y;

        ImU32 zoomOutColor = zoomOutHovered ? IM_COL32(80, 80, 80, 255) : IM_COL32(50, 50, 50, 255);
        fgDrawList->AddRectFilled(zoomOutMin, zoomOutMax, zoomOutColor, 4.0f);
        fgDrawList->AddLine(
            ImVec2(zoomOutX + 6, btnY + btnSize / 2),
            ImVec2(zoomOutX + btnSize - 6, btnY + btnSize / 2),
            IM_COL32(220, 220, 220, 255), 2.0f);

        if (zoomOutHovered && mouseClicked)
        {
            pendingZoomSteps = -1;
        }

        char zoomText[16];
        snprintf(zoomText, sizeof(zoomText), "%.0f%%", currentZoom * 100.0f);
        ImVec2 textSize = ImGui::CalcTextSize(zoomText);
        float textAreaWidth = 55;
        float textX = zoomOutX + btnSize + btnSpacing + (textAreaWidth - textSize.x) / 2;
        float textY = btnY + (btnSize - textSize.y) / 2;
        fgDrawList->AddText(ImVec2(textX, textY), IM_COL32(200, 200, 200, 255), zoomText);

        bool panelHovered = mousePos.x >= panelX && mousePos.x <= panelX + panelWidth &&
            mousePos.y >= panelY && mousePos.y <= panelY + panelHeight;
        if (panelHovered)
        {
            ImGui::SetTooltip("Press F to fit all nodes");
        }

        float zoomInX = zoomOutX + btnSize + btnSpacing + textAreaWidth + btnSpacing;
        ImVec2 zoomInMin(zoomInX, btnY);
        ImVec2 zoomInMax(zoomInX + btnSize, btnY + btnSize);
        bool zoomInHovered = mousePos.x >= zoomInMin.x && mousePos.x <= zoomInMax.x &&
            mousePos.y >= zoomInMin.y && mousePos.y <= zoomInMax.y;

        ImU32 zoomInColor = zoomInHovered ? IM_COL32(80, 80, 80, 255) : IM_COL32(50, 50, 50, 255);
        fgDrawList->AddRectFilled(zoomInMin, zoomInMax, zoomInColor, 4.0f);
        ImVec2 plusCenter(zoomInX + btnSize / 2, btnY + btnSize / 2);
        fgDrawList->AddLine(
            ImVec2(plusCenter.x - 6, plusCenter.y),
            ImVec2(plusCenter.x + 6, plusCenter.y),
            IM_COL32(220, 220, 220, 255), 2.0f);
        fgDrawList->AddLine(
            ImVec2(plusCenter.x, plusCenter.y - 6),
            ImVec2(plusCenter.x, plusCenter.y + 6),
            IM_COL32(220, 220, 220, 255), 2.0f);

        if (zoomInHovered && mouseClicked)
        {
            pendingZoomSteps = 1;
        }
    }

    ed::NodeId AnimatorNodeGraph::stateIdToNodeId(uint32_t stateId) const
    {
        return ed::NodeId(STATE_NODE_OFFSET + stateId);
    }

    ed::NodeId AnimatorNodeGraph::specialNodeId(int type) const
    {
        return ed::NodeId(SPECIAL_NODE_OFFSET + type);
    }

    ed::PinId AnimatorNodeGraph::statePinId(uint32_t stateId, bool isInput) const
    {
        return ed::PinId((isInput ? INPUT_PIN_OFFSET : OUTPUT_PIN_OFFSET) + stateId);
    }

    ed::PinId AnimatorNodeGraph::specialPinId(int type) const
    {
        return ed::PinId(SPECIAL_PIN_OFFSET + type);
    }

    ed::LinkId AnimatorNodeGraph::transitionIdToLinkId(uint32_t transitionId) const
    {
        return ed::LinkId(LINK_OFFSET + transitionId);
    }

    uint32_t AnimatorNodeGraph::nodeIdToStateId(ed::NodeId nodeId) const
    {
        uintptr_t id = nodeId.Get();
        if (id >= STATE_NODE_OFFSET)
        {
            return static_cast<uint32_t>(id - STATE_NODE_OFFSET);
        }
        return 0;
    }

    uint32_t AnimatorNodeGraph::linkIdToTransitionId(ed::LinkId linkId) const
    {
        uintptr_t id = linkId.Get();
        if (id >= LINK_OFFSET)
        {
            return static_cast<uint32_t>(id - LINK_OFFSET);
        }
        return 0;
    }
}
