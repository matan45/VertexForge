#pragma once

#include "../../../core/controllers/imguiHandler/ImguiWindow.hpp"
#include "../preview/PreviewWindowChrome.hpp"
#include "../../graph/BTGraphEditor.hpp"
#include "BTPropertyPanel.hpp"
#include "../../../utilities/behaviortree/BehaviorTreeTypes.hpp"
#include "../../../utilities/behaviortree/BehaviorTreeAsset.hpp"
#include "../../../utilities/behaviortree/BehaviorTreeValidation.hpp"
#include "events/EventTypes.hpp"
#include <atomic>
#include <string>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>

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
        // VK-1457: editor-session breakpoints (node ids) — authoritative here, pushed to the runtime.
        std::unordered_set<uint32_t> breakpoints;

        // VK-1457: authored static-SubTree node id -> expanded entry node id (from the snapshot). Used to
        // translate breakpoints to entry ids and mirror live status/active-path onto SubTree nodes, which
        // are removed from the runtime's expanded id space. augmented* are window-owned views handed to
        // the graph editor (pointers must outlive the frame). subtreeMapApplied re-pushes breakpoints once
        // the map first arrives with a snapshot.
        std::unordered_map<uint32_t, uint32_t> subtreeEntryMap;
        std::unordered_map<uint32_t, behaviortree::BTNodeStatus> augmentedNodeStatuses;
        std::vector<uint32_t> augmentedActivePath;
        bool subtreeMapApplied = false;

        behaviortree::validation::ValidationReport validationReport;
        std::unordered_map<uint32_t, behaviortree::validation::Severity> validationSeverities;
        // VK-1457 perf: full-graph revalidation is deferred until the active widget is released (see draw).
        bool validationDirty = false;

        // VK-1660: another writer of this .vfBehaviorTree (the MCP behavior-tree tools, through
        // SaveBehaviorTreeAssetCommand) announces the save with AssetSavedNotification. The handler only
        // raises externalChange; draw() reloads a clean window or shows the conflict banner.
        // EventDispatcher::publish invokes handlers AFTER releasing its lock, so the destructor's
        // unsubscribe() cannot stop a handler already in flight: it never touches `this`, and both flags
        // are shared with it.
        events::SubscriptionToken assetSavedToken;
        std::shared_ptr<std::atomic<bool>> externalChange = std::make_shared<std::atomic<bool>>(false);
        // Set while this window publishes its own save, so its handler skips that notification.
        std::shared_ptr<std::atomic<bool>> publishingOwnSave = std::make_shared<std::atomic<bool>>(false);
        bool showExternalChangeBanner = false;

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
        // Replaces the tree with the file on disk, keeping the graph view and the selection when its
        // node survives. Keeps what is on screen when the file cannot be read.
        void reloadFromDisk();
        void drawToolbar();
        void drawExternalChangeBanner();
        void drawDebugMenu();
        void drawGraphPanel();
        void drawPropertyPanel();
        void drawValidationPanel();
        void drawBlackboardPanel();
        void drawLiveBlackboardPanel();
        void drawDebugPanel();          // VK-1457: transport + breakpoints + history + aborts
        void pushBreakpointsToRuntime(); // VK-1457: send the current breakpoint set to the debug target
        void buildAugmentedDebugViews(); // VK-1457: mirror SubTree entry-node status/active-path onto SubTree ids
        void onGraphChanged();
        void revalidate();
        void updateDebugState();
        void startDebugging(services::EntityHandle entity);
        void stopDebugging();
    };
}
