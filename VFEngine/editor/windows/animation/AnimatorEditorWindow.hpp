#pragma once

#include "imguiHandler/ImguiWindow.hpp"
#include "AnimatorNodeGraph.hpp"
#include "AnimatorPropertiesPanel.hpp"
#include "AnimatorLayerPanel.hpp"
#include "BoneMaskEditorPanel.hpp"
#include "animator/AnimatorTypes.hpp"
#include "providers/PreviewInstanceId.hpp"
#include <string>
#include <memory>


namespace windows
{
    class AnimatorEditorWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::string animatorPath;
        std::string windowTitle;
        std::unique_ptr<animator::AnimatorData> animatorData;

        bool isOpen = true;
        bool needsInit = true;
        bool isDirty = false;
        bool needsPositionInit = false;
        bool needsNavigateToContent = false;
        int pendingZoomSteps = 0;

        uint32_t selectedStateId = 0;
        uint32_t selectedTransitionId = 0;

        bool showAddParameterPopup = false;
        std::string newParameterName;
        animator::AnimatorParameterType newParameterType = animator::AnimatorParameterType::Float;

        services::PreviewInstanceId instanceId;

        uint32_t selectedLayerIndex = 0;

        animation::AnimatorNodeGraph nodeGraph;
        animation::AnimatorPropertiesPanel propertiesPanel;
        animation::AnimatorLayerPanel layerPanel;
        animation::BoneMaskEditorPanel boneMaskPanel;
    public:
        explicit AnimatorEditorWindow(const std::string& path);
        ~AnimatorEditorWindow() override;

        void draw() override;
        bool shouldClose() const override { return !isOpen; }

        const std::string& getPath() const { return animatorPath; }

    private:
        void initEditor();
        void loadAnimator();
        void saveAnimator();

        void drawMenuBar();
        void drawToolbar();

        animator::AnimatorGraph* getActiveGraph();
        void ensureLayersInitialized();
    };
}
