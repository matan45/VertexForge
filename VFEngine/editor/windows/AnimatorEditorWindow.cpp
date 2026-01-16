#include "AnimatorEditorWindow.hpp"
#include "animator/AnimatorAsset.hpp"
#include "string/StringUtil.hpp"
#include "print/EditorLogger.hpp"
#include "imgui.h"
#include <imgui_node_editor.h>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace ed = ax::NodeEditor;
namespace fs = std::filesystem;

namespace windows
{
    AnimatorEditorWindow::AnimatorEditorWindow(const std::string& path)
        : animatorPath(path)
          , instanceId(services::PreviewInstanceId(this))
    {
        fs::path filePath(path);
        windowTitle = "Animator Editor - " + filePath.stem().string();
    }

    AnimatorEditorWindow::~AnimatorEditorWindow()
    {
        cleanUpNodeEditor();
    }

    void AnimatorEditorWindow::initEditor()
    {
        initNodeEditor();
        loadAnimator();
        needsPositionInit = true; // Initialize positions on first draw after loading
        needsNavigateToContent = true; // Navigate after positions are set
        needsInit = false;
    }

    void AnimatorEditorWindow::initNodeEditor()
    {
        ed::Config config;
        config.SettingsFile = nullptr; // Don't save settings to file
        config.NavigateButtonIndex = 1; // Middle mouse button for panning
        nodeEditorContext = ed::CreateEditor(&config);
    }

    void AnimatorEditorWindow::cleanUpNodeEditor()
    {
        if (nodeEditorContext)
        {
            ed::DestroyEditor(nodeEditorContext);
            nodeEditorContext = nullptr;
        }
    }

    void AnimatorEditorWindow::navigateToContent()
    {
        if (nodeEditorContext)
        {
            ed::SetCurrentEditor(nodeEditorContext);
            ed::NavigateToContent();
            ed::SetCurrentEditor(nullptr);
        }
    }

    void AnimatorEditorWindow::loadAnimator()
    {
        auto result = animator::AnimatorAsset::load(animatorPath);
        if (result)
        {
            animatorData = std::make_unique<animator::AnimatorData>(std::move(*result));
            isDirty = false;
            vfLogInfo("Loaded animator: {}", animatorPath);
        }
        else
        {
            vfLogError("Failed to load animator: {}", animatorPath);
            animatorData = std::make_unique<animator::AnimatorData>(
                animator::AnimatorAsset::createDefault("New Animator"));
        }
    }

    void AnimatorEditorWindow::saveAnimator()
    {
        if (!animatorData)
            return;

        if (animator::AnimatorAsset::save(animatorPath, *animatorData))
        {
            isDirty = false;
            vfLogInfo("Saved animator: {}", animatorPath);
        }
        else
        {
            vfLogError("Failed to save animator: {}", animatorPath);
        }
    }

    void AnimatorEditorWindow::draw()
    {
        if (needsInit)
        {
            initEditor();
        }

        std::string title = windowTitle + (isDirty ? " *###AnimatorEditor" : "###AnimatorEditor");

        ImGui::SetNextWindowSize(ImVec2(1200, 800), ImGuiCond_FirstUseEver);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar;
        if (ImGui::Begin(title.c_str(), &isOpen, flags))
        {
            drawMenuBar();
            drawToolbar();

            // Main content area - use full available height
            ImVec2 contentSize = ImGui::GetContentRegionAvail();

            // Wrap in a container child (like MaterialEditorWindow's TopRow pattern)
            ImGui::BeginChild("MainContent", ImVec2(0, contentSize.y), false, ImGuiWindowFlags_NoScrollbar);
            {
                ImVec2 innerSize = ImGui::GetContentRegionAvail();
                float panelWidth = 300.0f;
                float graphWidth = innerSize.x - panelWidth - ImGui::GetStyle().ItemSpacing.x;

                // Left side: Properties panel
                ImGui::BeginChild("PropertiesPanel", ImVec2(panelWidth, innerSize.y), true);

                if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    drawParametersPanel();
                }

                ImGui::Separator();

                if (selectedStateId != 0)
                {
                    if (ImGui::CollapsingHeader("State Properties", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        drawStatePropertiesPanel();
                    }
                }

                if (selectedTransitionId != 0)
                {
                    if (ImGui::CollapsingHeader("Transition Properties", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        drawTransitionPropertiesPanel();
                    }
                }

                ImGui::Separator();

                if (ImGui::CollapsingHeader("Playback", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    drawPlaybackControls();
                }

                ImGui::EndChild(); // PropertiesPanel

                ImGui::SameLine();

                // Right side: Node Graph
                ImGui::BeginChild("NodeGraphPanel", ImVec2(graphWidth, innerSize.y), true,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                drawNodeGraph();
                ImGui::EndChild(); // NodeGraphPanel
            }
            ImGui::EndChild(); // MainContent
        }
        ImGui::End();
    }

    void AnimatorEditorWindow::drawMenuBar()
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save", "Ctrl+S"))
                {
                    saveAnimator();
                }
                if (ImGui::MenuItem("Reload"))
                {
                    loadAnimator();
                    isDirty = false;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Close"))
                {
                    isOpen = false;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Add State"))
                {
                    if (animatorData)
                    {
                        animator::AnimatorState state;
                        state.id = animatorData->graph.nextStateId++;
                        state.name = "New State " + std::to_string(state.id);
                        state.position = glm::vec2(200.0f, 100.0f);
                        animatorData->graph.states.push_back(std::move(state));
                        isDirty = true;
                    }
                }
                if (ImGui::MenuItem("Add Parameter"))
                {
                    showAddParameterPopup = true;
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }
    }

    void AnimatorEditorWindow::drawToolbar()
    {
        if (ImGui::Button("Save"))
        {
            saveAnimator();
        }
        ImGui::SameLine();

        if (ImGui::Button("Add State"))
        {
            if (animatorData)
            {
                animator::AnimatorState state;
                state.id = animatorData->graph.nextStateId++;
                state.name = "New State " + std::to_string(state.id);
                state.position = glm::vec2(200.0f + (animatorData->graph.states.size() * 50.0f), 100.0f);
                animatorData->graph.states.push_back(std::move(state));
                isDirty = true;
            }
        }

        ImGui::Separator();
    }

    void AnimatorEditorWindow::drawNodeGraph()
    {
        if (!animatorData || !nodeEditorContext)
            return;

        ed::SetCurrentEditor(nodeEditorContext);

        // Store canvas info for zoom controls
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();

        // Fix: Prime ImGui layout state before node editor
        ImGui::Separator();

        // Pass explicit size to ensure canvas fills available space
        ed::Begin("AnimatorGraph");

        // Get current zoom level
        float currentZoom = ed::GetCurrentZoom();

        // Apply pending zoom
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

        // Initialize node positions when graph is loaded (must be done BEFORE drawing)
        if (needsPositionInit)
        {
            // Set special node positions
            ed::SetNodePosition(specialNodeId(ENTRY_NODE),
                                ImVec2(animatorData->graph.entryPosition.x, animatorData->graph.entryPosition.y));
            ed::SetNodePosition(specialNodeId(ANY_STATE_NODE),
                                ImVec2(animatorData->graph.anyStatePosition.x, animatorData->graph.anyStatePosition.y));

            // Set state node positions
            for (const auto& state : animatorData->graph.states)
            {
                ed::SetNodePosition(stateIdToNodeId(state.id),
                                    ImVec2(state.position.x, state.position.y));
            }

            needsPositionInit = false;

            // Navigate to content after positions are set
            if (needsNavigateToContent)
            {
                ed::NavigateToContent();
                needsNavigateToContent = false;
            }
        }

        // Draw special nodes (Entry, Any State)
        drawSpecialNodes();

        // Draw state nodes
        for (const auto& state : animatorData->graph.states)
        {
            drawStateNode(state);
        }

        // Draw transition links
        drawTransitionLinks();

        // Handle interactions
        handleNodeCreation();
        handleLinkCreation();
        handleDeletion();

        // Update selection
        updateSelection();

        // Sync positions back to data
        syncNodePositions();

        ed::End();

        // Draw zoom controls overlay
        drawZoomControls(canvasPos, canvasSize, currentZoom);

        ed::SetCurrentEditor(nullptr);

        // Handle add parameter popup
        if (showAddParameterPopup)
        {
            ImGui::OpenPopup("Add Parameter");
            showAddParameterPopup = false;
        }

        if (ImGui::BeginPopup("Add Parameter"))
        {
            ImGui::Text("Add New Parameter");
            ImGui::Separator();

            char buffer[256];
            std::strncpy(buffer, newParameterName.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            if (ImGui::InputText("Name", buffer, sizeof(buffer)))
            {
                newParameterName = buffer;
            }

            const char* types[] = {"Float", "Int", "Bool", "Trigger"};
            int typeIndex = static_cast<int>(newParameterType);
            if (ImGui::Combo("Type", &typeIndex, types, IM_ARRAYSIZE(types)))
            {
                newParameterType = static_cast<animator::AnimatorParameterType>(typeIndex);
            }

            if (ImGui::Button("Add") && !newParameterName.empty())
            {
                animator::AnimatorParameter param;
                param.name = newParameterName;
                param.type = newParameterType;

                switch (newParameterType)
                {
                case animator::AnimatorParameterType::Float:
                    param.defaultValue = 0.0f;
                    break;
                case animator::AnimatorParameterType::Int:
                    param.defaultValue = 0;
                    break;
                case animator::AnimatorParameterType::Bool:
                case animator::AnimatorParameterType::Trigger:
                    param.defaultValue = false;
                    break;
                }

                animatorData->graph.parameters.push_back(std::move(param));
                newParameterName.clear();
                isDirty = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                newParameterName.clear();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void AnimatorEditorWindow::drawSpecialNodes()
    {
        // Entry node
        ed::NodeId entryNodeId = specialNodeId(ENTRY_NODE);
        ed::PinId entryPinId = specialPinId(ENTRY_NODE);

        ed::BeginNode(entryNodeId);
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "Entry");
        ed::BeginPin(entryPinId, ed::PinKind::Output);
        ImGui::Text("->");
        ed::EndPin();
        ed::EndNode();

        // Any State node
        ed::NodeId anyStateNodeId = specialNodeId(ANY_STATE_NODE);
        ed::PinId anyStatePinId = specialPinId(ANY_STATE_NODE);

        ed::BeginNode(anyStateNodeId);
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1.0f), "Any State");
        ed::BeginPin(anyStatePinId, ed::PinKind::Output);
        ImGui::Text("->");
        ed::EndPin();
        ed::EndNode();
    }

    void AnimatorEditorWindow::drawStateNode(const animator::AnimatorState& state)
    {
        ed::NodeId nodeId = stateIdToNodeId(state.id);
        ed::PinId inputPinId = statePinId(state.id, true);
        ed::PinId outputPinId = statePinId(state.id, false);

        bool isDefault = (state.id == animatorData->graph.defaultStateId);

        // Node color
        const ImVec4& nodeColor = isDefault ? DEFAULT_STATE_COLOR : STATE_NODE_COLOR;
        ed::PushStyleColor(ed::StyleColor_NodeBg, nodeColor);

        ed::BeginNode(nodeId);

        // Input pin
        ed::BeginPin(inputPinId, ed::PinKind::Input);
        ImGui::Text("->");
        ed::EndPin();

        ImGui::SameLine();

        // Node content
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

        // Output pin
        ed::BeginPin(outputPinId, ed::PinKind::Output);
        ImGui::Text("->");
        ed::EndPin();

        ed::EndNode();
        ed::PopStyleColor();
    }

    void AnimatorEditorWindow::drawTransitionLinks()
    {
        for (const auto& transition : animatorData->graph.transitions)
        {
            ed::LinkId linkId = transitionIdToLinkId(transition.id);

            ed::PinId startPin;
            if (transition.sourceStateId == 0)
            {
                // From Any State
                startPin = specialPinId(ANY_STATE_NODE);
            }
            else
            {
                startPin = statePinId(transition.sourceStateId, false);
            }

            ed::PinId endPin = statePinId(transition.targetStateId, true);

            // Color based on conditions
            ImVec4 linkColor = transition.conditions.empty()
                                   ? ImVec4(0.8f, 0.8f, 0.8f, 1.0f)
                                   : ImVec4(0.4f, 0.8f, 1.0f, 1.0f);

            ed::Link(linkId, startPin, endPin, linkColor, 2.0f);
        }

        // Draw entry link to default state (use a unique link ID that won't conflict)
        if (animatorData->graph.defaultStateId != 0)
        {
            ed::PinId entryPin = specialPinId(ENTRY_NODE);
            ed::PinId defaultStatePin = statePinId(animatorData->graph.defaultStateId, true);
            // Use a special link ID (LINK_OFFSET + 0) for the entry->default link
            ed::Link(ed::LinkId(LINK_OFFSET), entryPin, defaultStatePin, ImVec4(0.3f, 0.9f, 0.3f, 1.0f), 3.0f);
        }
    }

    void AnimatorEditorWindow::handleNodeCreation()
    {
        if (ed::BeginCreate())
        {
            ed::PinId startPinId, endPinId;
            if (ed::QueryNewLink(&startPinId, &endPinId))
            {
                if (startPinId && endPinId && ed::AcceptNewItem())
                {
                    // Determine source and target states
                    uint32_t sourceStateId = 0;
                    uint32_t targetStateId = 0;

                    uintptr_t startId = startPinId.Get();
                    uintptr_t endId = endPinId.Get();

                    // Check if start is from special node
                    if (startId == SPECIAL_PIN_OFFSET + ANY_STATE_NODE)
                    {
                        sourceStateId = 0; // Any State
                    }
                    else if (startId >= OUTPUT_PIN_OFFSET)
                    {
                        sourceStateId = static_cast<uint32_t>(startId - OUTPUT_PIN_OFFSET);
                    }

                    // Check target state
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

    void AnimatorEditorWindow::handleLinkCreation()
    {
        // Already handled in handleNodeCreation
    }

    void AnimatorEditorWindow::handleDeletion()
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
                        // Remove state
                        auto& states = animatorData->graph.states;
                        states.erase(
                            std::remove_if(states.begin(), states.end(),
                                           [stateId](const animator::AnimatorState& s)
                                           {
                                               return s.id == stateId;
                                           }),
                            states.end());

                        // Remove transitions referencing this state
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

    void AnimatorEditorWindow::syncNodePositions()
    {
        // Sync state positions
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

        // Sync special node positions
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

    void AnimatorEditorWindow::updateSelection()
    {
        // Check node selection
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

    void AnimatorEditorWindow::clearSelection()
    {
        selectedStateId = 0;
        selectedTransitionId = 0;
        isEntrySelected = false;
        isAnyStateSelected = false;
    }

    // ID conversion helpers
    ed::NodeId AnimatorEditorWindow::stateIdToNodeId(uint32_t stateId) const
    {
        return ed::NodeId(STATE_NODE_OFFSET + stateId);
    }

    ed::NodeId AnimatorEditorWindow::specialNodeId(int type) const
    {
        return ed::NodeId(SPECIAL_NODE_OFFSET + type);
    }

    ed::PinId AnimatorEditorWindow::statePinId(uint32_t stateId, bool isInput) const
    {
        return ed::PinId((isInput ? INPUT_PIN_OFFSET : OUTPUT_PIN_OFFSET) + stateId);
    }

    ed::PinId AnimatorEditorWindow::specialPinId(int type) const
    {
        return ed::PinId(SPECIAL_PIN_OFFSET + type);
    }

    ed::LinkId AnimatorEditorWindow::transitionIdToLinkId(uint32_t transitionId) const
    {
        return ed::LinkId(LINK_OFFSET + transitionId);
    }

    uint32_t AnimatorEditorWindow::nodeIdToStateId(ed::NodeId nodeId) const
    {
        uintptr_t id = nodeId.Get();
        if (id >= STATE_NODE_OFFSET)
        {
            return static_cast<uint32_t>(id - STATE_NODE_OFFSET);
        }
        return 0;
    }

    uint32_t AnimatorEditorWindow::linkIdToTransitionId(ed::LinkId linkId) const
    {
        uintptr_t id = linkId.Get();
        if (id >= LINK_OFFSET)
        {
            return static_cast<uint32_t>(id - LINK_OFFSET);
        }
        return 0;
    }

    void AnimatorEditorWindow::drawParametersPanel()
    {
        if (!animatorData)
            return;

        if (ImGui::Button("Add Parameter"))
        {
            showAddParameterPopup = true;
        }

        ImGui::Separator();

        int indexToRemove = -1;
        for (size_t i = 0; i < animatorData->graph.parameters.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));

            auto& param = animatorData->graph.parameters[i];
            drawParameterEditor(param, i);

            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
            {
                indexToRemove = static_cast<int>(i);
            }

            ImGui::PopID();
        }

        if (indexToRemove >= 0)
        {
            animatorData->graph.parameters.erase(
                animatorData->graph.parameters.begin() + indexToRemove);
            isDirty = true;
        }
    }

    void AnimatorEditorWindow::drawParameterEditor(animator::AnimatorParameter& param, size_t index)
    {
        const char* typeNames[] = {"Float", "Int", "Bool", "Trigger"};
        ImGui::Text("%s (%s)", param.name.c_str(), typeNames[static_cast<int>(param.type)]);

        switch (param.type)
        {
        case animator::AnimatorParameterType::Float:
            {
                float val = std::get<float>(param.defaultValue);
                if (ImGui::DragFloat("##value", &val, 0.01f))
                {
                    param.defaultValue = val;
                    isDirty = true;
                }
            }
            break;
        case animator::AnimatorParameterType::Int:
            {
                int32_t val = std::get<int32_t>(param.defaultValue);
                if (ImGui::DragInt("##value", &val))
                {
                    param.defaultValue = val;
                    isDirty = true;
                }
            }
            break;
        case animator::AnimatorParameterType::Bool:
        case animator::AnimatorParameterType::Trigger:
            {
                bool val = std::get<bool>(param.defaultValue);
                if (ImGui::Checkbox("##value", &val))
                {
                    param.defaultValue = val;
                    isDirty = true;
                }
            }
            break;
        }
    }

    void AnimatorEditorWindow::drawStatePropertiesPanel()
    {
        if (!animatorData || selectedStateId == 0)
            return;

        auto* state = animatorData->graph.findStateById(selectedStateId);
        if (!state)
            return;

        // Name
        char nameBuffer[256];
        std::strncpy(nameBuffer, state->name.c_str(), sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
        if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
        {
            state->name = nameBuffer;
            isDirty = true;
        }

        // Animation path
        ImGui::Text("Animation:");
        if (!state->animationPath.empty())
        {
            fs::path animPath(state->animationPath);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "%s", animPath.filename().string().c_str());
        }

        if (ImGui::Button("Select Animation"))
        {
            nfd::FileDialog fileDialog;
            std::string path = fileDialog.openFileDialog(
                {{L"VF Animation Files (*.vfAnim)", L"*.vfAnim"}});
            if (!path.empty())
            {
                state->animationPath = path;
                isDirty = true;
            }
        }

        if (!state->animationPath.empty())
        {
            ImGui::SameLine();
            if (ImGui::Button("Clear##Animation"))
            {
                state->animationPath.clear();
                isDirty = true;
            }
        }

        // Playback speed
        if (ImGui::DragFloat("Speed", &state->playbackSpeed, 0.01f, 0.0f, 10.0f))
        {
            isDirty = true;
        }

        // Loop
        if (ImGui::Checkbox("Loop", &state->loop))
        {
            isDirty = true;
        }

        // Set as default
        bool isDefault = (state->id == animatorData->graph.defaultStateId);
        if (ImGui::Checkbox("Default State", &isDefault))
        {
            if (isDefault)
            {
                animatorData->graph.defaultStateId = state->id;
            }
            isDirty = true;
        }
    }

    void AnimatorEditorWindow::drawTransitionPropertiesPanel()
    {
        if (!animatorData || selectedTransitionId == 0)
            return;

        animator::AnimatorTransition* transition = nullptr;
        for (auto& t : animatorData->graph.transitions)
        {
            if (t.id == selectedTransitionId)
            {
                transition = &t;
                break;
            }
        }

        if (!transition)
            return;

        // Source and target info
        std::string sourceName = transition->sourceStateId == 0 ? "Any State" : "Unknown";
        std::string targetName = "Unknown";

        if (transition->sourceStateId != 0)
        {
            if (auto* s = animatorData->graph.findStateById(transition->sourceStateId))
                sourceName = s->name;
        }
        if (auto* t = animatorData->graph.findStateById(transition->targetStateId))
            targetName = t->name;

        ImGui::Text("%s -> %s", sourceName.c_str(), targetName.c_str());
        ImGui::Separator();

        // Blend duration
        if (ImGui::DragFloat("Blend Duration", &transition->blendDuration, 0.01f, 0.0f, 5.0f))
        {
            isDirty = true;
        }

        // Has exit time
        if (ImGui::Checkbox("Has Exit Time", &transition->hasExitTime))
        {
            isDirty = true;
        }

        if (transition->hasExitTime)
        {
            if (ImGui::DragFloat("Exit Time", &transition->exitTime, 0.01f, 0.0f, 1.0f))
            {
                isDirty = true;
            }
        }

        // Priority
        if (ImGui::DragInt("Priority", &transition->priority))
        {
            isDirty = true;
        }

        // Conditions
        ImGui::Separator();
        ImGui::Text("Conditions:");

        int conditionToRemove = -1;
        for (size_t i = 0; i < transition->conditions.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            drawConditionEditor(transition->conditions[i], *transition, i);

            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
            {
                conditionToRemove = static_cast<int>(i);
            }
            ImGui::PopID();
        }

        if (conditionToRemove >= 0)
        {
            transition->conditions.erase(transition->conditions.begin() + conditionToRemove);
            isDirty = true;
        }

        if (ImGui::Button("Add Condition"))
        {
            animator::TransitionCondition cond;
            if (!animatorData->graph.parameters.empty())
            {
                cond.parameterName = animatorData->graph.parameters[0].name;
                cond.op = animator::ComparisonOperator::Equal;
                cond.value = animatorData->graph.parameters[0].defaultValue;
            }
            transition->conditions.push_back(std::move(cond));
            isDirty = true;
        }
    }

    void AnimatorEditorWindow::drawConditionEditor(animator::TransitionCondition& condition,
                                                   animator::AnimatorTransition& transition,
                                                   size_t index)
    {
        // Parameter selection
        if (ImGui::BeginCombo("##param", condition.parameterName.c_str()))
        {
            for (const auto& param : animatorData->graph.parameters)
            {
                bool isSelected = (condition.parameterName == param.name);
                if (ImGui::Selectable(param.name.c_str(), isSelected))
                {
                    condition.parameterName = param.name;
                    condition.value = param.defaultValue;
                    isDirty = true;
                }
            }
            ImGui::EndCombo();
        }

        // Find parameter type
        animator::AnimatorParameterType paramType = animator::AnimatorParameterType::Float;
        for (const auto& param : animatorData->graph.parameters)
        {
            if (param.name == condition.parameterName)
            {
                paramType = param.type;
                break;
            }
        }

        // Operator selection
        ImGui::SameLine();
        const char* opNames[] = {"==", "!=", ">", "<", ">=", "<="};
        int opIndex = static_cast<int>(condition.op);
        ImGui::SetNextItemWidth(50);
        if (ImGui::Combo("##op", &opIndex, opNames, IM_ARRAYSIZE(opNames)))
        {
            condition.op = static_cast<animator::ComparisonOperator>(opIndex);
            isDirty = true;
        }

        // Value based on type
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);

        switch (paramType)
        {
        case animator::AnimatorParameterType::Float:
            {
                float val = std::get<float>(condition.value);
                if (ImGui::DragFloat("##val", &val, 0.01f))
                {
                    condition.value = val;
                    isDirty = true;
                }
            }
            break;
        case animator::AnimatorParameterType::Int:
            {
                int32_t val = std::get<int32_t>(condition.value);
                if (ImGui::DragInt("##val", &val))
                {
                    condition.value = val;
                    isDirty = true;
                }
            }
            break;
        case animator::AnimatorParameterType::Bool:
        case animator::AnimatorParameterType::Trigger:
            {
                bool val = std::get<bool>(condition.value);
                if (ImGui::Checkbox("##val", &val))
                {
                    condition.value = val;
                    isDirty = true;
                }
            }
            break;
        }
    }

    void AnimatorEditorWindow::drawPlaybackControls()
    {
        ImGui::Text("Preview playback controls");
        ImGui::Text("(Not yet implemented)");

        // Placeholder for future preview integration
        if (ImGui::Button("Play"))
        {
            // TODO: Start playback
        }
        ImGui::SameLine();
        if (ImGui::Button("Pause"))
        {
            // TODO: Pause playback
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop"))
        {
            // TODO: Stop playback
        }
    }

    void AnimatorEditorWindow::drawZoomControls(ImVec2 canvasPos, ImVec2 canvasSize, float currentZoom)
    {
        ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();

        // Position in bottom-right of the canvas
        float panelX = canvasPos.x + canvasSize.x - 130;
        float panelY = canvasPos.y + canvasSize.y - 40;
        float panelWidth = 125;
        float panelHeight = 35;

        // Draw background panel
        fgDrawList->AddRectFilled(
            ImVec2(panelX, panelY),
            ImVec2(panelX + panelWidth, panelY + panelHeight),
            IM_COL32(30, 30, 30, 220), 6.0f);
        fgDrawList->AddRect(
            ImVec2(panelX, panelY),
            ImVec2(panelX + panelWidth, panelY + panelHeight),
            IM_COL32(60, 60, 60, 255), 6.0f);

        // Button dimensions
        float btnSize = 25;
        float btnY = panelY + 5;
        float btnSpacing = 5;

        // Get mouse position for hit testing
        ImVec2 mousePos = ImGui::GetMousePos();
        bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

        // Zoom out button (-)
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

        // Zoom percentage text
        char zoomText[16];
        snprintf(zoomText, sizeof(zoomText), "%.0f%%", currentZoom * 100.0f);
        ImVec2 textSize = ImGui::CalcTextSize(zoomText);
        float textAreaWidth = 55;
        float textX = zoomOutX + btnSize + btnSpacing + (textAreaWidth - textSize.x) / 2;
        float textY = btnY + (btnSize - textSize.y) / 2;
        fgDrawList->AddText(ImVec2(textX, textY), IM_COL32(200, 200, 200, 255), zoomText);

        // Tooltip for the panel
        bool panelHovered = mousePos.x >= panelX && mousePos.x <= panelX + panelWidth &&
            mousePos.y >= panelY && mousePos.y <= panelY + panelHeight;
        if (panelHovered)
        {
            ImGui::SetTooltip("Press F to fit all nodes");
        }

        // Zoom in button (+)
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
}
