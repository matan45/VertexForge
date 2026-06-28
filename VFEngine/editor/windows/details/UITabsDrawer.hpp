#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"
#include <vector>

namespace windows::details
{
    class UITabsDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

        // VK-1442 — structural authoring for the tabs compound widget, shared by the inspector
        // (+ Add / - Remove buttons) and the UI Layer Builder's auto-assembly. These dispatch the
        // same CQRS commands the rest of the editor uses and read the registry to keep the tabs
        // invariant intact: a tabs root's children are [tab bar, pane 0, pane 1, ...]; the tab
        // bar's children are the tab buttons; and tab-button count == pane count.

        struct TabsStructure
        {
            services::EntityHandle tabBar = services::EntityHandle::invalid();
            std::vector<services::EntityHandle> tabButtons; // tab bar's direct children
            std::vector<services::EntityHandle> panes;      // root's children excluding the tab bar
        };

        // Splits the tabs root into its tab bar / buttons / panes (all invalid/empty when absent).
        static TabsStructure inspectStructure(services::EntityHandle tabsRoot);

        // Adds one tab: a button under the tab bar (created if absent) plus a content pane under
        // the root, keeping the button==pane invariant. Returns the new pane (invalid on failure).
        static services::EntityHandle addTab(services::EntityHandle tabsRoot);

        // Removes the last tab button + last pane (no-op when there are none); clamps activeTabIndex.
        static void removeLastTab(services::EntityHandle tabsRoot);

        // Sets each pane's active flag to (paneIndex == activeIndex) so the preview shows exactly
        // the selected tab's content pane.
        static void syncPaneVisibility(services::EntityHandle tabsRoot, int activeIndex);

        // True (and clears the flag) when the last draw() performed a structural edit — add/remove
        // tab or an active-tab change. The UI Layer Builder polls this to rebuild its preview, since
        // those edits are driven by buttons/combos whose effect lands on the mouse-release frame.
        bool consumeStructuralChange();

    private:
        bool drawHeader(bool& outRemove);
        bool drawTabBarPosition(services::UITabsData& data);
        bool drawActiveTabSelector(services::UITabsData& data, int paneCount);
        bool drawTabAuthoring(services::EntityHandle handle, int paneCount);
        void drawCurrentState(const services::UITabsData& data);

        bool structuralChangePending_ = false;
    };
}
