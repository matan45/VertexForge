#include "AnimatorNodeGraph.hpp"
#include <IconsFontAwesome6.h>
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
                                  int& pendingZoomSteps,
                                  const services::AnimatorRuntimeDebugData* debugData)
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
            drawStateNode(state, animatorData->graph.defaultStateId, debugData);
        }

        drawTransitionLinks(animatorData, debugData);

        handleNodeCreation(animatorData, isDirty);
        handleDeletion(animatorData, selectedStateId, selectedTransitionId, isDirty);
        handleCopyPaste(animatorData, isDirty);

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

    void AnimatorNodeGraph::drawStateNode(const animator::AnimatorState& state, uint32_t defaultStateId,
                                          const services::AnimatorRuntimeDebugData* debugData)
    {
        ed::NodeId nodeId = stateIdToNodeId(state.id);
        ed::PinId inputPinId = statePinId(state.id, true);
        ed::PinId outputPinId = statePinId(state.id, false);

        bool isDefault = (state.id == defaultStateId);
        bool isActiveState = debugData && debugData->currentStateId == state.id;
        bool isPreviousState = debugData && debugData->isBlending && debugData->previousStateId == state.id;

        const ImVec4& nodeColor = isDefault ? DEFAULT_STATE_COLOR : STATE_NODE_COLOR;
        ed::PushStyleColor(ed::StyleColor_NodeBg, nodeColor);

        // Active state: pulsing green border
        if (isActiveState)
        {
            float pulse = 0.6f + 0.4f * std::sin(static_cast<float>(ImGui::GetTime()) * 3.0f);
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.2f, pulse, 0.2f, 1.0f));
            ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 3.0f);
        }
        else if (isPreviousState)
        {
            float fade = 1.0f - debugData->blendProgress;
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.9f, 0.7f, 0.2f, fade));
            ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 2.5f);
        }

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
        if (isActiveState)
        {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), ICON_FA_PLAY " Active");
        }
        if (state.blendTree.has_value())
        {
            const char* btLabel = state.blendTree->type == animator::BlendTreeType::BlendTree1D
                ? "[Blend 1D]" : "[Blend 2D]";
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", btLabel);
        }
        else if (state.animationRef.isValid())
        {
            fs::path animPath(state.animationRef.resolve());
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", animPath.filename().string().c_str());
        }
        ImGui::EndGroup();

        ImGui::SameLine();

        ed::BeginPin(outputPinId, ed::PinKind::Output);
        ImGui::Text("->");
        ed::EndPin();

        ed::EndNode();

        if (isActiveState || isPreviousState)
        {
            ed::PopStyleVar();
            ed::PopStyleColor();
        }
        ed::PopStyleColor();
    }

    void AnimatorNodeGraph::drawTransitionLinks(animator::AnimatorData* animatorData,
                                                const services::AnimatorRuntimeDebugData* debugData)
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

            bool isActive = debugData && debugData->activeTransitionId == transition.id && debugData->isBlending;

            ImVec4 linkColor;
            float thickness;
            if (isActive)
            {
                float pulse = 0.7f + 0.3f * std::sin(static_cast<float>(ImGui::GetTime()) * 4.0f);
                linkColor = ImVec4(0.2f, pulse, 0.2f, 1.0f);
                thickness = 4.0f;
            }
            else
            {
                linkColor = transition.conditions.empty()
                    ? ImVec4(0.8f, 0.8f, 0.8f, 1.0f)
                    : ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
                thickness = 2.0f;
            }

            ed::Link(linkId, startPin, endPin, linkColor, thickness);
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

    static bool isInRect(ImVec2 pos, ImVec2 mn, ImVec2 mx)
    {
        return pos.x >= mn.x && pos.x <= mx.x && pos.y >= mn.y && pos.y <= mx.y;
    }

    void AnimatorNodeGraph::drawZoomControls(ImVec2 canvasPos, ImVec2 canvasSize, float currentZoom, int& pendingZoomSteps)
    {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        float px = canvasPos.x + canvasSize.x - 130, py = canvasPos.y + canvasSize.y - 40;
        float pw = 125, ph = 35, bs = 25, by = py + 5, sp = 5;

        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + ph), IM_COL32(30, 30, 30, 220), 6.0f);
        dl->AddRect(ImVec2(px, py), ImVec2(px + pw, py + ph), IM_COL32(60, 60, 60, 255), 6.0f);

        ImVec2 mp = ImGui::GetMousePos();
        bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

        float ox = px + 5;
        ImVec2 oMin(ox, by), oMax(ox + bs, by + bs);
        bool oHov = isInRect(mp, oMin, oMax);
        dl->AddRectFilled(oMin, oMax, oHov ? IM_COL32(80, 80, 80, 255) : IM_COL32(50, 50, 50, 255), 4.0f);
        dl->AddLine(ImVec2(ox + 6, by + bs / 2), ImVec2(ox + bs - 6, by + bs / 2), IM_COL32(220, 220, 220, 255), 2.0f);
        if (oHov && click) pendingZoomSteps = -1;

        char zoomText[16]; snprintf(zoomText, sizeof(zoomText), "%.0f%%", currentZoom * 100.0f);
        ImVec2 ts = ImGui::CalcTextSize(zoomText);
        float tw = 55;
        dl->AddText(ImVec2(ox + bs + sp + (tw - ts.x) / 2, by + (bs - ts.y) / 2), IM_COL32(200, 200, 200, 255), zoomText);

        if (isInRect(mp, ImVec2(px, py), ImVec2(px + pw, py + ph))) ImGui::SetTooltip("Press F to fit all nodes");

        float ix = ox + bs + sp + tw + sp;
        ImVec2 iMin(ix, by), iMax(ix + bs, by + bs);
        bool iHov = isInRect(mp, iMin, iMax);
        dl->AddRectFilled(iMin, iMax, iHov ? IM_COL32(80, 80, 80, 255) : IM_COL32(50, 50, 50, 255), 4.0f);
        ImVec2 pc(ix + bs / 2, by + bs / 2);
        dl->AddLine(ImVec2(pc.x - 6, pc.y), ImVec2(pc.x + 6, pc.y), IM_COL32(220, 220, 220, 255), 2.0f);
        dl->AddLine(ImVec2(pc.x, pc.y - 6), ImVec2(pc.x, pc.y + 6), IM_COL32(220, 220, 220, 255), 2.0f);
        if (iHov && click) pendingZoomSteps = 1;
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

    void AnimatorNodeGraph::handleCopyPaste(animator::AnimatorData* animatorData, bool& isDirty)
    {
        if (!animatorData)
            return;

        bool canHandleKeys = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
        if (!canHandleKeys) return;

        ImGuiIO& io = ImGui::GetIO();

        // Copy: Ctrl+C
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
        {
            std::vector<ed::NodeId> selectedNodes;
            int count = ed::GetSelectedObjectCount();
            selectedNodes.resize(count);
            int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), count);

            clipboard.clear();
            for (int i = 0; i < nodeCount; ++i)
            {
                uint32_t stateId = nodeIdToStateId(selectedNodes[i]);
                if (stateId == 0)
                    continue;

                for (const auto& state : animatorData->graph.states)
                {
                    if (state.id == stateId)
                    {
                        clipboard.push_back(state);
                        break;
                    }
                }
            }
        }

        // Paste: Ctrl+V
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && !clipboard.empty())
        {
            glm::vec2 offset(50.0f, 50.0f);

            for (const auto& copiedState : clipboard)
            {
                animator::AnimatorState newState = copiedState;
                newState.id = animatorData->graph.nextStateId++;
                newState.name = copiedState.name + " (Copy)";
                newState.position = copiedState.position + offset;
                animatorData->graph.states.push_back(std::move(newState));
            }

            isDirty = true;
        }
    }
}
