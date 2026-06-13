#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"
#include "events/EventDispatcher.hpp"
#include <string>
#include <unordered_set>
#include <vector>

namespace windows
{
    class SceneHierarchyPanel : public controllers::imguiHandler::ImguiWindow
    {
    private:
        services::EntityHandle selectedHandle; // primary selection (synced with global)
        services::EntityHandle lastSelectedHandle;
        std::vector<services::EntityHandle> selectedHandles;
        events::SubscriptionToken sceneClearedToken;

        // Panel-owned persistent open state (SetNextItemOpen every frame)
        std::unordered_set<uint64_t> expandedHandles;
        bool rootSeeded = false;

        std::unordered_set<uint64_t> hiddenEntities;
        std::unordered_set<uint64_t> lockedEntities;
        int entityTypeFilter = 0;
        char nameSearchBuffer[128] = {};

        // Inline rename. Sentinel must be INVALID_ID, not 0: entt uses 0 as a valid
        // entity id (the scene Root), so a 0 sentinel would make isRenaming true for
        // the Root every frame and pin the rename box open on its row.
        uint64_t renamingHandle = services::EntityHandle::INVALID_ID;
        char renameBuffer[256] = {};
        bool renameFocusPending = false;

        // Shift-range selection over last frame's visible draw order. INVALID_ID
        // sentinel for the same reason as renamingHandle (Root entity id == 0).
        uint64_t rangeAnchorHandle = services::EntityHandle::INVALID_ID;
        std::vector<services::EntityHandle> visibleOrder;
        std::vector<services::EntityHandle> lastVisibleOrder;

        // Copy/paste clipboard: one prefab-format JSON string per copied subtree
        std::vector<std::string> clipboardEntities;

    public:
        explicit SceneHierarchyPanel();
        ~SceneHierarchyPanel() override;

        void draw() override;

    private:
        void drawToolbar(const services::SceneHierarchyData& hierarchy);
        void drawEntityNode(services::EntityHandle handle);
        void drawSearchResults(const services::SceneHierarchyData& hierarchy);
        void drawContextMenu(const services::SceneHierarchyData& hierarchy);
        void drawReorderDropZone(services::EntityHandle parent, size_t index);
        void dragDropEntity(services::EntityHandle handle);
        void handleShortcuts();

        void handleSelectionClick(services::EntityHandle handle);
        bool isSelected(services::EntityHandle handle) const;
        bool isEntityDragActive() const;

        void beginRename(services::EntityHandle handle);
        void drawRenameInput(services::EntityHandle handle);

        void copySelection();
        void pasteClipboard();
        void duplicateSelection();
        void deleteSelection();
        // Selected entities minus the root and minus any entity whose ancestor is also selected
        std::vector<services::EntityHandle> collectTopLevelSelection() const;

        void expandAll(const services::SceneHierarchyData& hierarchy);
        void collapseAll();
        void expandToSelection(services::EntityHandle handle);

        void subscribeToEvents();
        void onSceneCleared();
        const char* getEntityIcon(const services::EntityData& data) const;
        bool matchesTypeFilter(const services::EntityData& data) const;
        bool matchesNameSearch(const std::string& name) const;
    };
}
