#pragma once

#include "animator/AnimatorTypes.hpp"
#include <imgui_node_editor.h>
#include <imgui.h>

namespace ax::NodeEditor { struct EditorContext; }

namespace windows::animation
{
    class AnimatorNodeGraph
    {
    public:
        void init();
        void cleanUp();

        void draw(animator::AnimatorData* animatorData,
                  uint32_t& selectedStateId,
                  uint32_t& selectedTransitionId,
                  bool& isDirty,
                  bool& needsPositionInit,
                  bool& needsNavigateToContent,
                  int& pendingZoomSteps);

        ax::NodeEditor::EditorContext* getContext() const { return nodeEditorContext; }

    private:
        void drawSpecialNodes();
        void drawStateNode(const animator::AnimatorState& state, uint32_t defaultStateId);
        void drawTransitionLinks(animator::AnimatorData* animatorData);
        void handleNodeCreation(animator::AnimatorData* animatorData, bool& isDirty);
        void handleDeletion(animator::AnimatorData* animatorData,
                            uint32_t& selectedStateId,
                            uint32_t& selectedTransitionId,
                            bool& isDirty);
        void syncNodePositions(animator::AnimatorData* animatorData, bool& isDirty);
        void updateSelection(uint32_t& selectedStateId, uint32_t& selectedTransitionId);
        void drawZoomControls(ImVec2 canvasPos, ImVec2 canvasSize, float currentZoom, int& pendingZoomSteps);

        ax::NodeEditor::NodeId stateIdToNodeId(uint32_t stateId) const;
        ax::NodeEditor::NodeId specialNodeId(int type) const;
        ax::NodeEditor::PinId statePinId(uint32_t stateId, bool isInput) const;
        ax::NodeEditor::PinId specialPinId(int type) const;
        ax::NodeEditor::LinkId transitionIdToLinkId(uint32_t transitionId) const;

        uint32_t nodeIdToStateId(ax::NodeEditor::NodeId nodeId) const;
        uint32_t linkIdToTransitionId(ax::NodeEditor::LinkId linkId) const;

        void handleCopyPaste(animator::AnimatorData* animatorData, bool& isDirty);

    private:
        std::vector<animator::AnimatorState> clipboard;

        ax::NodeEditor::EditorContext* nodeEditorContext = nullptr;

        static constexpr uintptr_t SPECIAL_NODE_OFFSET = 100;
        static constexpr uintptr_t STATE_NODE_OFFSET = 1000;
        static constexpr uintptr_t SPECIAL_PIN_OFFSET = 500;
        static constexpr uintptr_t INPUT_PIN_OFFSET = 10000;
        static constexpr uintptr_t OUTPUT_PIN_OFFSET = 20000;
        static constexpr uintptr_t LINK_OFFSET = 30000;

        static constexpr int ENTRY_NODE = 0;
        static constexpr int ANY_STATE_NODE = 1;

        static inline const ImVec4 STATE_NODE_COLOR = ImVec4(60/255.0f, 120/255.0f, 180/255.0f, 1.0f);
        static inline const ImVec4 DEFAULT_STATE_COLOR = ImVec4(80/255.0f, 180/255.0f, 80/255.0f, 1.0f);
    };
}
