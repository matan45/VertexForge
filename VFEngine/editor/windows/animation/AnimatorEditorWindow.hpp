#pragma once

#include "imguiHandler/ImguiWindow.hpp"
#include "../preview/PreviewWindowChrome.hpp"
#include "AnimatorNodeGraph.hpp"
#include "AnimatorPropertiesPanel.hpp"
#include "AnimatorLayerPanel.hpp"
#include "BoneMaskEditorPanel.hpp"
#include "animator/AnimatorTypes.hpp"
#include "animator/AnimatorValidation.hpp"
#include "events/EventTypes.hpp"
#include "providers/PreviewInstanceId.hpp"
#include <atomic>
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

        editor::preview::WindowMaximizer maximizer;
        ImVec2 initialSize{0.0f, 0.0f};
        bool sizeSaved = false;

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

        // Search
        char searchBuffer[256] = {};

        // Validation
        std::vector<animator::GraphIssue> validationIssues;
        bool showValidationPanel = false;

        // VK-1656: another writer of this .vfAnimator (the MCP animation tools and their undo,
        // through SaveAnimatorAssetCommand) announces the save with AssetSavedNotification. The
        // handler only raises externalChange; draw() reloads a clean window or shows the conflict
        // banner. EventDispatcher::publish invokes handlers AFTER releasing its lock, so the
        // destructor's unsubscribe() cannot stop a handler already in flight: it never touches
        // `this`, and both flags are shared with it.
        events::SubscriptionToken assetSavedToken;
        std::shared_ptr<std::atomic<bool>> externalChange = std::make_shared<std::atomic<bool>>(false);
        // Set while this window publishes its own save, so its handler skips that notification.
        std::shared_ptr<std::atomic<bool>> publishingOwnSave = std::make_shared<std::atomic<bool>>(false);
        bool showExternalChangeBanner = false;

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
        // Replaces animatorData with the file on disk, keeping what still applies (layer,
        // selection). Never between draw()'s layer-graph swap-in and swap-back.
        void reloadFromDisk();

        void drawMenuBar();
        void drawToolbar();
        void drawExternalChangeBanner();

        // Selects the issue's state/transition, switching to its layer when needed.
        void focusIssue(const animator::GraphIssue& issue);
        bool isOnOtherLayer(const animator::GraphIssue& issue) const;
        bool issueLayerExists(const animator::GraphIssue& issue) const;

        // The selected layer's graph. Outside draw()'s swap only: while the layer graph is
        // swapped into animatorData->graph, the layer slot holds the stale top-level copy.
        animator::AnimatorGraph* getActiveGraph();
        // A DirectClip layer has no state machine and the serializer never writes its graph, so its
        // states and parameters are not editable (the edits would be lost on save).
        bool directClipLayerSelected() const;
        void ensureLayersInitialized();
    };
}
