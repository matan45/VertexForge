#pragma once

#include "../../../core/controllers/imguiHandler/ImguiWindow.hpp"
#include "../../graph/BTGraphEditor.hpp"
#include "BTPropertyPanel.hpp"
#include "../../../utilities/behaviortree/BehaviorTreeTypes.hpp"
#include "../../../utilities/behaviortree/BehaviorTreeAsset.hpp"
#include <string>
#include <memory>

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

        editor::graph::BTGraphEditor graphEditor;
        BTPropertyPanel propertyPanel;

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
        void drawGraphPanel();
        void drawPropertyPanel();
        void drawBlackboardPanel();
        void onGraphChanged();
    };
}
