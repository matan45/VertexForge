#pragma once

#include "../../../core/controllers/imguiHandler/ImguiWindow.hpp"
#include "../preview/PreviewWindowChrome.hpp"
#include "../../graph/BTGraphEditor.hpp"
#include "BTPropertyPanel.hpp"
#include "../../../utilities/behaviortree/BehaviorTreeTypes.hpp"
#include "../../../utilities/behaviortree/BehaviorTreeAsset.hpp"
#include "../../../utilities/behaviortree/BehaviorTreeValidation.hpp"
#include <string>
#include <memory>
#include <unordered_map>

namespace editor::windows
{
    class BehaviorTreeEditorWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::string treePath;
        std::string windowTitle;
        std::unique_ptr<behaviortree::BehaviorTreeData> treeData;
        bool isOpen = true;
        bool needsInit = true;
        bool isDirty = false;

        ::editor::preview::WindowMaximizer maximizer;
        ImVec2 initialSize{0.0f, 0.0f};
        bool sizeSaved = false;

        editor::graph::BTGraphEditor graphEditor;
        BTPropertyPanel propertyPanel;

        // Live debugging of a running tree instance (play mode only)
        bool debugActive = false;
        services::EntityHandle debugTarget;
        behaviortree::BTRuntimeSnapshot debugSnapshot;

        behaviortree::validation::ValidationReport validationReport;
        std::unordered_map<uint32_t, behaviortree::validation::Severity> validationSeverities;

    public:
        explicit BehaviorTreeEditorWindow(const std::string& path);
        ~BehaviorTreeEditorWindow() override;

        void draw() override;
        bool shouldClose() const override { return !isOpen; }

        const std::string& getTreePath() const { return treePath; }

    private:
        void initEditor();
        void loadTree();
        void saveTree();
        void drawToolbar();
        void drawDebugMenu();
        void drawGraphPanel();
        void drawPropertyPanel();
        void drawValidationPanel();
        void drawBlackboardPanel();
        void drawLiveBlackboardPanel();
        void onGraphChanged();
        void revalidate();
        void updateDebugState();
        void startDebugging(services::EntityHandle entity);
        void stopDebugging();
    };
}
