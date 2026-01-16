#pragma once

#include "imguiHandler/ImguiWindow.hpp"
#include "animator/AnimatorTypes.hpp"
#include "providers/PreviewInstanceId.hpp"
#include <imgui_node_editor.h>
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <unordered_map>

namespace ax::NodeEditor { struct EditorContext; }

namespace windows
{
    class AnimatorEditorWindow : public controllers::imguiHandler::ImguiWindow
    {
    public:
        explicit AnimatorEditorWindow(const std::string& path);
        ~AnimatorEditorWindow() override;

        void draw() override;
        bool shouldClose() const override { return !isOpen; }

        const std::string& getPath() const { return animatorPath; }

    private:
        // Initialization
        void initEditor();
        void initNodeEditor();
        void cleanUpNodeEditor();
        void navigateToContent();

        // File operations
        void loadAnimator();
        void saveAnimator();

        // UI Drawing
        void drawMenuBar();
        void drawToolbar();
        void drawNodeGraph();
        void drawParametersPanel();
        void drawStatePropertiesPanel();
        void drawTransitionPropertiesPanel();
        void drawPlaybackControls();
        void drawZoomControls(ImVec2 canvasPos, ImVec2 canvasSize, float currentZoom);

        // Node graph helpers
        void drawStateNode(const animator::AnimatorState& state);
        void drawSpecialNodes();
        void drawTransitionLinks();
        void handleNodeCreation();
        void handleLinkCreation();
        void handleDeletion();
        void syncNodePositions();

        // Selection handling
        void updateSelection();
        void clearSelection();

        // ID conversion helpers
        ax::NodeEditor::NodeId stateIdToNodeId(uint32_t stateId) const;
        ax::NodeEditor::NodeId specialNodeId(int type) const;  // 0 = Entry, 1 = Any State
        ax::NodeEditor::PinId statePinId(uint32_t stateId, bool isInput) const;
        ax::NodeEditor::PinId specialPinId(int type) const;
        ax::NodeEditor::LinkId transitionIdToLinkId(uint32_t transitionId) const;

        uint32_t nodeIdToStateId(ax::NodeEditor::NodeId nodeId) const;
        uint32_t linkIdToTransitionId(ax::NodeEditor::LinkId linkId) const;

        // Parameter helpers
        void drawParameterEditor(animator::AnimatorParameter& param, size_t index);
        void addParameter();

        // Transition condition helpers
        void drawConditionEditor(animator::TransitionCondition& condition,
                                 animator::AnimatorTransition& transition, size_t index);

    private:
        std::string animatorPath;
        std::string windowTitle;
        std::unique_ptr<animator::AnimatorData> animatorData;

        // Node editor context
        ax::NodeEditor::EditorContext* nodeEditorContext = nullptr;

        // Window state
        bool isOpen = true;
        bool needsInit = true;
        bool isDirty = false;
        bool needsPositionInit = false;
        bool needsNavigateToContent = false;
        int pendingZoomSteps = 0;

        // Selection state
        uint32_t selectedStateId = 0;
        uint32_t selectedTransitionId = 0;
        bool isEntrySelected = false;
        bool isAnyStateSelected = false;

        // New item state
        bool showAddParameterPopup = false;
        std::string newParameterName;
        animator::AnimatorParameterType newParameterType = animator::AnimatorParameterType::Float;

        // Preview instance
        services::PreviewInstanceId instanceId;

        // ID offsets to avoid conflicts
        static constexpr uintptr_t STATE_NODE_OFFSET = 1000;
        static constexpr uintptr_t SPECIAL_NODE_OFFSET = 100;
        static constexpr uintptr_t INPUT_PIN_OFFSET = 10000;
        static constexpr uintptr_t OUTPUT_PIN_OFFSET = 20000;
        static constexpr uintptr_t SPECIAL_PIN_OFFSET = 500;
        static constexpr uintptr_t LINK_OFFSET = 30000;

        // Special node types
        static constexpr int ENTRY_NODE = 0;
        static constexpr int ANY_STATE_NODE = 1;

        // Node colors (as ImVec4 for node editor API)
        static inline const ImVec4 STATE_NODE_COLOR = ImVec4(60/255.0f, 120/255.0f, 180/255.0f, 1.0f);
        static inline const ImVec4 DEFAULT_STATE_COLOR = ImVec4(80/255.0f, 180/255.0f, 80/255.0f, 1.0f);
        static inline const ImVec4 ENTRY_NODE_COLOR = ImVec4(50/255.0f, 150/255.0f, 50/255.0f, 1.0f);
        static inline const ImVec4 ANY_STATE_COLOR = ImVec4(180/255.0f, 120/255.0f, 60/255.0f, 1.0f);
    };
}
